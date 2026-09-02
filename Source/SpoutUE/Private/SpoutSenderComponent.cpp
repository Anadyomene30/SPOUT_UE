// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutSenderComponent.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "RHICommandList.h"
#include "RenderingThread.h"
#include "SpoutSettings.h"
#include "SpoutUE.h"
#include "TextureResource.h"
#include "Widgets/SWindow.h"

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
		return World && World->IsGameWorld();
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

	if (SenderName.IsEmpty())
	{
		UE_LOG(LogSpout, Error, TEXT("A Spout sender needs a name before it can start."));
		return;
	}

	Sender = FSpoutSenderHandle::Create();
	if (!Sender.IsValid())
	{
		UE_LOG(LogSpout, Error, TEXT("Sender '%s' cannot start: Spout needs the D3D12 RHI on Windows."), *SenderName);
		return;
	}

	bIsSending = true;
	LastSendTimeSeconds = 0.0;
	LastPublishedName.Reset();

	if (SourceMode == ESpoutSourceMode::Viewport)
	{
		BindViewportSource();
	}
}

void USpoutSenderComponent::StopSending()
{
	if (!bIsSending && !Sender.IsValid())
	{
		return;
	}

	const FString StoppedName = LastPublishedName.IsEmpty() ? SenderName : LastPublishedName;
	const bool bWasPublished = !LastPublishedName.IsEmpty();

	bIsSending = false;
	UnbindViewportSource();
	DestroyStream();
	LastPublishedName.Reset();

	if (bWasPublished)
	{
		OnSenderStopped.Broadcast(StoppedName);
	}
}

void USpoutSenderComponent::DestroyStream()
{
	if (Sender.IsValid())
	{
		Sender->Destroy();
		Sender.Reset();
	}

#if PLATFORM_WINDOWS
	if (ViewportStaging.IsValid())
	{
		ENQUEUE_RENDER_COMMAND(SpoutReleaseViewportStaging)(
			[this](FRHICommandListImmediate& RHICmdList)
			{
				ViewportStaging.SafeRelease();
			});

		// The staging texture is only ever touched on the render thread, and this
		// is the last command that will touch it, so the component waits rather
		// than risk being destroyed out from under it.
		FlushRenderingCommands();
	}
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
		UE_LOG(LogSpout, Warning, TEXT("Sender '%s': the game viewport has no window yet, so the viewport source will stay idle."), *SenderName);
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
	// before this component can consider itself detached from it.
	FlushRenderingCommands();
#endif
}

void USpoutSenderComponent::OnBackBufferReady_RenderThread(SWindow& Window, const FTextureRHIRef& BackBuffer)
{
#if PLATFORM_WINDOWS
	check(IsInRenderingThread());

	if (!bIsSending || !BackBuffer.IsValid() || !Sender.IsValid())
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

	FSpoutSenderSettings Settings;
	{
		FScopeLock Lock(&ViewportSettingsSection);
		Settings.SenderName = ViewportSenderName;
		Settings.PixelFormat = ViewportPixelFormat;
		Settings.ColorSpace = ViewportColorSpace;
		Settings.AlphaMode = ViewportAlphaMode;
		Settings.bFlipVertical = bViewportFlipVertical;
	}

	if (Settings.SenderName.IsEmpty())
	{
		return;
	}

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	const FIntPoint BackBufferExtent(BackBuffer->GetSizeX(), BackBuffer->GetSizeY());

	// The swap chain buffer is not something to sample from or hand to another
	// device, so the presented frame is copied into a texture of our own first.
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

	Sender->Send_RenderThread(RHICmdList, ViewportStaging.GetReference(), Settings);
#endif
}

void USpoutSenderComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIsSending || !Sender.IsValid())
	{
		return;
	}

	if (SourceMode == ESpoutSourceMode::Viewport)
	{
		// The viewport path is driven by the present callback, so all this tick
		// does is keep the render thread's copy of the settings current.
		{
			FScopeLock Lock(&ViewportSettingsSection);
			ViewportSenderName = SenderName;
			ViewportPixelFormat = PixelFormat;
			ViewportColorSpace = ColorSpace;
			ViewportAlphaMode = AlphaMode;
			bViewportFlipVertical = bFlipVertical;
		}

		NotifyPublishedName();
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
	if (!SourceRenderTarget || !Sender.IsValid())
	{
		return;
	}

	FTextureRenderTargetResource* Resource = SourceRenderTarget->GameThread_GetRenderTargetResource();
	if (!Resource)
	{
		return;
	}

	FSpoutSenderSettings Settings;
	Settings.SenderName = SenderName;
	Settings.PixelFormat = PixelFormat;
	Settings.ColorSpace = ColorSpace;
	Settings.AlphaMode = AlphaMode;
	Settings.bFlipVertical = bFlipVertical;

	TSharedPtr<FSpoutSenderHandle> SenderPtr = Sender;

	ENQUEUE_RENDER_COMMAND(SpoutSendFrame)(
		[SenderPtr, Resource, Settings](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* SourceTexture = Resource->GetRenderTargetTexture();
			if (!SourceTexture)
			{
				SourceTexture = Resource->TextureRHI;
			}

			SenderPtr->Send_RenderThread(RHICmdList, SourceTexture, Settings);
		});

	NotifyPublishedName();
}

void USpoutSenderComponent::NotifyPublishedName()
{
	// The published name only becomes known once the render thread has created
	// the sender, so it is picked up on a later tick rather than synchronously.
	const FString Published = Sender.IsValid() ? Sender->GetPublishedName() : FString();

	if (!Published.IsEmpty() && Published != LastPublishedName)
	{
		LastPublishedName = Published;
		OnSenderStarted.Broadcast(Published);
		UE_LOG(LogSpout, Log, TEXT("Spout sender '%s' is live."), *Published);
	}
}

bool USpoutSenderComponent::IsSending() const
{
	return bIsSending && Sender.IsValid() && Sender->IsSending();
}

void USpoutSenderComponent::SetSenderName(const FString& NewName)
{
	if (NewName == SenderName)
	{
		return;
	}

	const bool bWasSending = bIsSending;

	// Spout identifies a sender by its name, so a rename is a new sender.
	if (bWasSending)
	{
		StopSending();
	}

	SenderName = NewName;

	if (bWasSending)
	{
		StartSending();
	}
}

void USpoutSenderComponent::SetSourceRenderTarget(UTextureRenderTarget2D* NewRenderTarget)
{
	SourceRenderTarget = NewRenderTarget;
}

FString USpoutSenderComponent::GetPublishedSenderName() const
{
	return Sender.IsValid() ? Sender->GetPublishedName() : FString();
}

FIntPoint USpoutSenderComponent::GetPublishedSize() const
{
	return Sender.IsValid() ? Sender->GetPublishedSize() : FIntPoint::ZeroValue;
}

FSpoutStreamStats USpoutSenderComponent::GetStats() const
{
	return Sender.IsValid() ? Sender->GetStats() : FSpoutStreamStats();
}

#if WITH_EDITOR
void USpoutSenderComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	// Name, format, source and startup policy all change the identity or the
	// shape of the published sender, so the stream is rebuilt rather than
	// mutated in place.
	static const TSet<FName> RestartProperties =
	{
		GET_MEMBER_NAME_CHECKED(USpoutSenderComponent, SenderName),
		GET_MEMBER_NAME_CHECKED(USpoutSenderComponent, PixelFormat),
		GET_MEMBER_NAME_CHECKED(USpoutSenderComponent, SourceMode),
		GET_MEMBER_NAME_CHECKED(USpoutSenderComponent, StartupPolicy),
	};

	if (RestartProperties.Contains(PropertyName))
	{
		StopSending();

		if (ShouldAutoStart())
		{
			StartSending();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
