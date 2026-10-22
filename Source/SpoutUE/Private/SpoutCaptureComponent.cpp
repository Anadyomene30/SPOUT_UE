// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutCaptureComponent.h"

#include "Blueprint/UserWidget.h"
#include "Camera/CameraComponent.h"
#include "CineCameraComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "SpoutConversionShader.h"
#include "SpoutUE.h"
#include "TextureResource.h"

namespace
{
	ETextureRenderTargetFormat ToRenderTargetFormat(ESpoutPixelFormat Format)
	{
		switch (Format)
		{
		case ESpoutPixelFormat::RGB10A2:	return RTF_RGB10A2;
		case ESpoutPixelFormat::RGBA16F:	return RTF_RGBA16f;
		case ESpoutPixelFormat::RGBA32F:	return RTF_RGBA32f;
		case ESpoutPixelFormat::BGRA8:
		case ESpoutPixelFormat::RGBA8:
		case ESpoutPixelFormat::Auto:
		default:							return RTF_RGBA8;
		}
	}

	/** Alpha only survives the post process chain when the project propagates it. */
	bool IsAlphaPropagationEnabled()
	{
		static const IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(TEXT("r.PostProcessing.PropagateAlpha"));
		return CVar && CVar->GetInt() != 0;
	}
}

USpoutCaptureComponent::USpoutCaptureComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	bTickInEditor = true;

	// The capture is driven from TickComponent so that the frame rate cap and
	// the pause switch actually mean something; letting the scene capture run
	// itself would ignore both.
	bCaptureEveryFrame = false;
	bCaptureOnMovement = false;

	CaptureSource = ESceneCaptureSource::SCS_FinalToneCurveHDR;
	bAlwaysPersistRenderingState = true;
}

void USpoutCaptureComponent::OnRegister()
{
	Super::OnRegister();

	if (bCaptureEnabled)
	{
		RebuildCaptureTarget();
	}
}

void USpoutCaptureComponent::OnUnregister()
{
	ReleaseBurnIn();
	ReleaseCaptureTarget();

	Super::OnUnregister();
}

FIntPoint USpoutCaptureComponent::GetCaptureSize() const
{
	int32 Width = 0;
	int32 Height = 0;

	if (SpoutUE::GetPresetResolution(Resolution, Width, Height))
	{
		return FIntPoint(Width, Height);
	}

	return FIntPoint(
		FMath::Clamp(CustomResolution.X, 1, 16384),
		FMath::Clamp(CustomResolution.Y, 1, 16384));
}

void USpoutCaptureComponent::RebuildCaptureTarget()
{
	const FIntPoint Size = GetCaptureSize();

	if (CaptureTarget && CaptureTarget->SizeX == Size.X && CaptureTarget->SizeY == Size.Y
		&& CaptureTarget->RenderTargetFormat == ToRenderTargetFormat(PixelFormat))
	{
		return;
	}

	ReleaseCaptureTarget();

	CaptureTarget = NewObject<UTextureRenderTarget2D>(this, NAME_None, RF_Transient);
	CaptureTarget->RenderTargetFormat = ToRenderTargetFormat(PixelFormat);
	CaptureTarget->ClearColor = bCaptureAlpha ? FLinearColor::Transparent : FLinearColor::Black;

	// The scene capture writes scene-referred values; a Spout sender applies the
	// transfer function on the way out, so the target must not apply one too.
	CaptureTarget->bForceLinearGamma = true;
	CaptureTarget->TargetGamma = 1.f;
	CaptureTarget->InitAutoFormat(Size.X, Size.Y);
	CaptureTarget->UpdateResourceImmediate(true);

	TextureTarget = CaptureTarget;

	if (bCaptureAlpha && !IsAlphaPropagationEnabled())
	{
		UE_LOG(LogSpout, Warning,
			TEXT("'%s' captures alpha, but r.PostProcessing.PropagateAlpha is off, so the alpha channel will come out opaque. Enable 'Support alpha channel in post processing' in Project Settings > Rendering."),
			*GetPathName());
	}

	// The burn-in target has to follow the capture size.
	if (BurnInTarget)
	{
		ReleaseBurnIn();
	}
}

void USpoutCaptureComponent::ReleaseCaptureTarget()
{
	TextureTarget = nullptr;

	if (CaptureTarget)
	{
		CaptureTarget->ReleaseResource();
		CaptureTarget = nullptr;
	}
}

void USpoutCaptureComponent::EnsureBurnIn()
{
	if (!BurnInWidgetClass || !CaptureTarget)
	{
		return;
	}

	if (!BurnInWidget)
	{
		UWorld* World = GetWorld();
		if (!World)
		{
			return;
		}

		BurnInWidget = CreateWidget<UUserWidget>(World, BurnInWidgetClass);
		if (!BurnInWidget)
		{
			return;
		}
	}

	if (!BurnInRenderer)
	{
		// No gamma correction: the burn-in is composited into a linear capture
		// target, and the sender encodes the whole frame afterwards.
		BurnInRenderer = new FWidgetRenderer(false);
	}

	const FIntPoint Size = GetCaptureSize();

	if (!BurnInTarget || BurnInTarget->SizeX != Size.X || BurnInTarget->SizeY != Size.Y)
	{
		if (BurnInTarget)
		{
			BurnInTarget->ReleaseResource();
		}

		BurnInTarget = NewObject<UTextureRenderTarget2D>(this, NAME_None, RF_Transient);
		BurnInTarget->RenderTargetFormat = RTF_RGBA8;
		BurnInTarget->ClearColor = FLinearColor::Transparent;
		BurnInTarget->bForceLinearGamma = true;
		BurnInTarget->InitAutoFormat(Size.X, Size.Y);
		BurnInTarget->UpdateResourceImmediate(true);
	}
}

void USpoutCaptureComponent::ReleaseBurnIn()
{
	if (BurnInRenderer)
	{
		// FWidgetRenderer may still be referenced by in-flight render commands.
		BeginCleanup(BurnInRenderer);
		BurnInRenderer = nullptr;
	}

	if (BurnInTarget)
	{
		BurnInTarget->ReleaseResource();
		BurnInTarget = nullptr;
	}

	BurnInWidget = nullptr;
}

void USpoutCaptureComponent::DrawBurnIn(float DeltaTime)
{
	EnsureBurnIn();

	if (!BurnInRenderer || !BurnInWidget || !BurnInTarget || !CaptureTarget)
	{
		return;
	}

	const FIntPoint Size = GetCaptureSize();
	const TSharedRef<SWidget> SlateWidget = BurnInWidget->TakeWidget();

	BurnInRenderer->DrawWidget(BurnInTarget, SlateWidget, FVector2D(Size.X, Size.Y), DeltaTime, false);

	FTextureRenderTargetResource* OverlayResource = BurnInTarget->GameThread_GetRenderTargetResource();
	FTextureRenderTargetResource* CaptureResource = CaptureTarget->GameThread_GetRenderTargetResource();

	if (!OverlayResource || !CaptureResource)
	{
		return;
	}

	ENQUEUE_RENDER_COMMAND(SpoutComposeBurnIn)(
		[OverlayResource, CaptureResource](FRHICommandListImmediate& RHICmdList)
		{
			SpoutUE::ComposeOverlay_RenderThread(
				RHICmdList,
				OverlayResource->GetRenderTargetTexture(),
				CaptureResource->GetRenderTargetTexture());
		});
}

void USpoutCaptureComponent::SyncWithAttachedCamera()
{
	if (!bFollowAttachedCamera)
	{
		return;
	}

	const UCameraComponent* Camera = Cast<UCameraComponent>(GetAttachParent());
	if (!Camera)
	{
		return;
	}

	FOVAngle = Camera->FieldOfView;
	ProjectionType = Camera->ProjectionMode;
	OrthoWidth = Camera->OrthoWidth;

	// Post process on the camera is what carries exposure, depth of field and
	// colour grading; without this the output would not match what the operator
	// framed through that camera.
	PostProcessSettings = Camera->PostProcessSettings;
	PostProcessBlendWeight = Camera->PostProcessBlendWeight;
}

void USpoutCaptureComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bCaptureEnabled)
	{
		if (CaptureTarget)
		{
			ReleaseBurnIn();
			ReleaseCaptureTarget();
		}
		return;
	}

	if (!CaptureTarget)
	{
		RebuildCaptureTarget();
	}

	if (bPaused)
	{
		return;
	}

	if (MaxCaptureFrameRate > 0.f)
	{
		const double Now = FPlatformTime::Seconds();
		const double MinInterval = 1.0 / static_cast<double>(MaxCaptureFrameRate);

		if (LastCaptureTimeSeconds > 0.0 && (Now - LastCaptureTimeSeconds) < MinInterval)
		{
			return;
		}

		LastCaptureTimeSeconds = Now;
	}

	SyncWithAttachedCamera();
	CaptureScene();

	if (BurnInWidgetClass)
	{
		DrawBurnIn(DeltaTime);
	}
	else if (BurnInWidget || BurnInTarget || BurnInRenderer)
	{
		ReleaseBurnIn();
	}
}

#if WITH_EDITOR
void USpoutCaptureComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	static const TSet<FName> RebuildProperties =
	{
		GET_MEMBER_NAME_CHECKED(USpoutCaptureComponent, Resolution),
		GET_MEMBER_NAME_CHECKED(USpoutCaptureComponent, CustomResolution),
		GET_MEMBER_NAME_CHECKED(USpoutCaptureComponent, PixelFormat),
		GET_MEMBER_NAME_CHECKED(USpoutCaptureComponent, bCaptureAlpha),
	};

	if (RebuildProperties.Contains(PropertyName))
	{
		ReleaseBurnIn();
		ReleaseCaptureTarget();

		if (bCaptureEnabled)
		{
			RebuildCaptureTarget();
		}
	}

	if (PropertyName == GET_MEMBER_NAME_CHECKED(USpoutCaptureComponent, BurnInWidgetClass))
	{
		ReleaseBurnIn();
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
