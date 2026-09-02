// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutSenderComponent.h"

#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"
#include "SpoutSettings.h"
#include "SpoutUE.h"
#include "TextureResource.h"

#if PLATFORM_WINDOWS
#include "D3D12/SpoutSenderStream.h"
#endif

USpoutSenderComponent::USpoutSenderComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;

	// Sharing a render target is just as useful while dressing a scene as it is
	// in PIE, so the component ticks in the editor too. The startup policy is
	// what decides whether anything is actually published.
	bTickInEditor = true;

	const USpoutSettings* Settings = USpoutSettings::Get();
	SenderName = Settings->DefaultSenderName;
	ColorSpace = Settings->DefaultSenderColorSpace;
	AlphaMode = Settings->DefaultSenderAlphaMode;
}

void USpoutSenderComponent::OnRegister()
{
	Super::OnRegister();

	if (ShouldAutoStart())
	{
		StartSending();
	}
}

void USpoutSenderComponent::OnUnregister()
{
	StopSending();
	Super::OnUnregister();
}

void USpoutSenderComponent::BeginPlay()
{
	Super::BeginPlay();

	if (ShouldAutoStart())
	{
		StartSending();
	}
}

void USpoutSenderComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopSending();
	Super::EndPlay(EndPlayReason);
}

bool USpoutSenderComponent::ShouldAutoStart() const
{
	switch (StartupPolicy)
	{
	case ESpoutStartupPolicy::EditorAndGame:
		return true;

	case ESpoutStartupPolicy::GameOnly:
	{
		const UWorld* World = GetWorld();
		return World && (World->IsGameWorld());
	}

	case ESpoutStartupPolicy::Manual:
	default:
		return false;
	}
}

void USpoutSenderComponent::StartSending()
{
	if (bIsSending)
	{
		return;
	}

	if (!FSpoutUEModule::IsRHISupported())
	{
		UE_LOG(LogSpout, Error, TEXT("Sender '%s' cannot start: Spout needs the D3D12 RHI on Windows."), *SenderName);
		return;
	}

	if (SenderName.IsEmpty())
	{
		UE_LOG(LogSpout, Error, TEXT("A Spout sender needs a name before it can start."));
		return;
	}

	bIsSending = true;
	LastSendTimeSeconds = 0.0;
	LastPublishedName.Reset();
}

void USpoutSenderComponent::StopSending()
{
	if (!bIsSending && !Stream)
	{
		return;
	}

	const FString StoppedName = LastPublishedName.IsEmpty() ? SenderName : LastPublishedName;

	bIsSending = false;
	DestroyStream();

	if (!LastPublishedName.IsEmpty())
	{
		LastPublishedName.Reset();
		OnSenderStopped.Broadcast(StoppedName);
	}
}

void USpoutSenderComponent::DestroyStream()
{
#if PLATFORM_WINDOWS
	if (!Stream)
	{
		return;
	}

	// The stream owns D3D resources that may only be touched on the render
	// thread, so ownership is handed over rather than deleted here.
	FSpoutSenderStream* Released = Stream;
	Stream = nullptr;

	ENQUEUE_RENDER_COMMAND(SpoutReleaseSender)(
		[Released](FRHICommandListImmediate& RHICmdList)
		{
			Released->Release_RenderThread();
			delete Released;
		});
#endif
}

void USpoutSenderComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIsSending)
	{
		return;
	}

	if (MaxFrameRate > 0.f)
	{
		const double Now = FPlatformTime::Seconds();
		const double MinInterval = 1.0 / static_cast<double>(MaxFrameRate);

		if (LastSendTimeSeconds > 0.0 && (Now - LastSendTimeSeconds) < MinInterval)
		{
			return;
		}

		LastSendTimeSeconds = Now;
	}

	SendFrame();
}

void USpoutSenderComponent::SendFrame()
{
#if PLATFORM_WINDOWS
	if (!SourceRenderTarget)
	{
		return;
	}

	FTextureRenderTargetResource* Resource = SourceRenderTarget->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		return;
	}

	if (!Stream)
	{
		Stream = new FSpoutSenderStream();
	}

	FSpoutSendParams Params;
	Params.SenderName = SenderName;
	Params.PixelFormat = PixelFormat;
	Params.Conversion.ColorSpace = ColorSpace;
	Params.Conversion.AlphaMode = AlphaMode;
	Params.Conversion.bFlipVertical = bFlipVertical;

	FSpoutSenderStream* StreamPtr = Stream;

	ENQUEUE_RENDER_COMMAND(SpoutSendFrame)(
		[StreamPtr, Resource, Params](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* SourceTexture = Resource->GetRenderTargetTexture();
			if (!SourceTexture)
			{
				SourceTexture = Resource->TextureRHI;
			}

			StreamPtr->Send_RenderThread(RHICmdList, SourceTexture, Params);
		});

	// The published name only becomes known once the render thread has created
	// the sender, so it is picked up on a later tick rather than synchronously.
	const FString Published = Stream->GetPublishedName();
	if (!Published.IsEmpty() && Published != LastPublishedName)
	{
		LastPublishedName = Published;
		OnSenderStarted.Broadcast(Published);
		UE_LOG(LogSpout, Log, TEXT("Spout sender '%s' is live."), *Published);
	}
#endif
}

bool USpoutSenderComponent::IsSending() const
{
#if PLATFORM_WINDOWS
	return bIsSending && Stream && Stream->IsSending();
#else
	return false;
#endif
}

void USpoutSenderComponent::SetSenderName(const FString& NewName)
{
	if (NewName == SenderName)
	{
		return;
	}

	SenderName = NewName;

	// Spout identifies a sender by its name, so a rename is a new sender.
	if (bIsSending)
	{
		const bool bWasSending = bIsSending;
		StopSending();

		if (bWasSending)
		{
			StartSending();
		}
	}
}

void USpoutSenderComponent::SetSourceRenderTarget(UTextureRenderTarget2D* NewRenderTarget)
{
	SourceRenderTarget = NewRenderTarget;
}

FString USpoutSenderComponent::GetPublishedSenderName() const
{
#if PLATFORM_WINDOWS
	return Stream ? Stream->GetPublishedName() : FString();
#else
	return FString();
#endif
}

FIntPoint USpoutSenderComponent::GetPublishedSize() const
{
#if PLATFORM_WINDOWS
	return Stream ? Stream->GetPublishedSize() : FIntPoint::ZeroValue;
#else
	return FIntPoint::ZeroValue;
#endif
}

FSpoutStreamStats USpoutSenderComponent::GetStats() const
{
#if PLATFORM_WINDOWS
	return Stream ? Stream->GetStats() : FSpoutStreamStats();
#else
	return FSpoutStreamStats();
#endif
}

#if WITH_EDITOR
void USpoutSenderComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	// Name, format and startup policy all change the identity or the shape of the
	// published sender, so the stream is rebuilt rather than mutated in place.
	static const TSet<FName> RestartProperties =
	{
		GET_MEMBER_NAME_CHECKED(USpoutSenderComponent, SenderName),
		GET_MEMBER_NAME_CHECKED(USpoutSenderComponent, PixelFormat),
		GET_MEMBER_NAME_CHECKED(USpoutSenderComponent, StartupPolicy),
	};

	if (RestartProperties.Contains(PropertyName))
	{
		const bool bShouldRun = bIsSending || ShouldAutoStart();
		StopSending();

		if (bShouldRun && ShouldAutoStart())
		{
			StartSending();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
