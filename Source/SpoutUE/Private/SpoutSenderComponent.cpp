// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutSenderComponent.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "RHICommandList.h"
#include "RenderingThread.h"
#include "Widgets/SWindow.h"
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

#if PLATFORM_WINDOWS
	// Created here rather than lazily: in viewport mode the send runs from a
	// present callback, and a pointer written by one thread and read by the other
	// is a race worth not having.
	if (!Stream)
	{
		Stream = new FSpoutSenderStream();
	}
#endif

	if (SourceMode == ESpoutSourceMode::Viewport)
	{
		BindViewportSource();
	}
}

void USpoutSenderComponent::StopSending()
{
	if (!bIsSending && !Stream)
	{
		return;
	}

	const FString StoppedName = LastPublishedName.IsEmpty() ? SenderName : LastPublishedName;

	bIsSending = false;
	UnbindViewportSource();
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
		[this, Released](FRHICommandListImmediate& RHICmdList)
		{
			Released->Release_RenderThread();
			delete Released;

			ViewportStaging.SafeRelease();
		});

	// The staging texture is only touched on the render thread, and the command
	// above is the last thing that will touch it, so waiting here keeps the
	// component from being destroyed out from under it.
	FlushRenderingCommands();
#endif
}

void USpoutSenderComponent::BindViewportSource()
{
#if PLATFORM_WINDOWS
	if (bViewportSourceBound)
	{
		return;
	}

	if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
	{
		UE_LOG(LogSpout, Error, TEXT("Sender '%s' cannot use the viewport source: Slate is not available (running headless?)."), *SenderName);
		return;
	}

	if (!GEngine || !GEngine->GameViewport)
	{
		UE_LOG(LogSpout, Warning,
			TEXT("Sender '%s' is set to the viewport source but there is no game viewport. This mode works in PIE and packaged builds; use a render target to output from the editor viewport."),
			*SenderName);
		return;
	}

	GameViewportWindow = GEngine->GameViewport->GetWindow();
	if (!GameViewportWindow.IsValid())
	{
		UE_LOG(LogSpout, Warning, TEXT("Sender '%s': the game viewport has no window yet; the viewport source will stay idle."), *SenderName);
		return;
	}

	FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().AddRaw(this, &USpoutSenderComponent::OnBackBufferReady_RenderThread);
	bViewportSourceBound = true;
#endif
}

void USpoutSenderComponent::UnbindViewportSource()
{
#if PLATFORM_WINDOWS
	if (!bViewportSourceBound)
	{
		return;
	}

	if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
	{
		FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().RemoveAll(this);
	}

	bViewportSourceBound = false;
	GameViewportWindow.Reset();

	// The delegate fires on the render thread, so an in-flight call has to finish
	// before this component can be considered detached from it.
	FlushRenderingCommands();
#endif
}

void USpoutSenderComponent::OnBackBufferReady_RenderThread(SWindow& Window, const FTextureRHIRef& BackBuffer)
{
#if PLATFORM_WINDOWS
	check(IsInRenderingThread());

	if (!bIsSending || !BackBuffer.IsValid())
	{
		return;
	}

	// Slate presents every window it owns, including editor tool windows, so the
	// game viewport's own window is the only one worth copying.
	const TSharedPtr<SWindow> TargetWindow = GameViewportWindow.Pin();
	if (!TargetWindow.IsValid() || TargetWindow.Get() != &Window)
	{
		return;
	}

	FSpoutSendParams Params;
	{
		FScopeLock Lock(&ViewportSettingsSection);
		Params.SenderName = ViewportSenderName;
		Params.PixelFormat = ViewportPixelFormat;
		Params.Conversion.ColorSpace = ViewportColorSpace;
		Params.Conversion.AlphaMode = ViewportAlphaMode;
		Params.Conversion.bFlipVertical = bViewportFlipVertical;
	}

	if (Params.SenderName.IsEmpty())
	{
		return;
	}

	if (!Stream)
	{
		return;
	}

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	const FIntPoint BackBufferExtent(BackBuffer->GetSizeX(), BackBuffer->GetSizeY());

	// The swap chain buffer is not something to sample from or to hand to another
	// device, so the frame is copied into a texture of our own first.
	if (!ViewportStaging.IsValid()
		|| ViewportStaging->GetSizeX() != BackBufferExtent.X
		|| ViewportStaging->GetSizeY() != BackBufferExtent.Y
		|| ViewportStaging->GetFormat() != BackBuffer->GetFormat())
	{
		const FRHITextureCreateDesc Desc =
			FRHITextureCreateDesc::Create2D(TEXT("Spout.ViewportStaging"))
			.SetExtent(BackBufferExtent)
			.SetFormat(BackBuffer->GetFormat())
			.SetNumMips(1)
			.SetFlags(ETextureCreateFlags::ShaderResource | ETextureCreateFlags::RenderTargetable)
			.SetInitialState(ERHIAccess::CopyDest);

		ViewportStaging = RHICmdList.CreateTexture(Desc);
	}

	if (!ViewportStaging.IsValid())
	{
		return;
	}

	RHICmdList.Transition(FRHITransitionInfo(BackBuffer, ERHIAccess::Unknown, ERHIAccess::CopySrc));
	RHICmdList.Transition(FRHITransitionInfo(ViewportStaging, ERHIAccess::Unknown, ERHIAccess::CopyDest));
	RHICmdList.CopyTexture(BackBuffer, ViewportStaging, FRHICopyTextureInfo());

	// Hand the swap chain buffer back in the state the presenter expects.
	RHICmdList.Transition(FRHITransitionInfo(BackBuffer, ERHIAccess::CopySrc, ERHIAccess::Present));

	Stream->Send_RenderThread(RHICmdList, ViewportStaging.GetReference(), Params);
#endif
}

void USpoutSenderComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIsSending)
	{
		return;
	}

	if (SourceMode == ESpoutSourceMode::Viewport)
	{
		// The viewport path is driven by the present callback, so all this tick
		// does is keep the render thread's copy of the settings current and
		// surface the published name once Spout has created the sender.
		{
			FScopeLock Lock(&ViewportSettingsSection);
			ViewportSenderName = SenderName;
			ViewportPixelFormat = PixelFormat;
			ViewportColorSpace = ColorSpace;
			ViewportAlphaMode = AlphaMode;
			bViewportFlipVertical = bFlipVertical;
		}

		if (Stream)
		{
			const FString Published = Stream->GetPublishedName();
			if (!Published.IsEmpty() && Published != LastPublishedName)
			{
				LastPublishedName = Published;
				OnSenderStarted.Broadcast(Published);
				UE_LOG(LogSpout, Log, TEXT("Spout sender '%s' is live (viewport)."), *Published);
			}
		}

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
		return;
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
