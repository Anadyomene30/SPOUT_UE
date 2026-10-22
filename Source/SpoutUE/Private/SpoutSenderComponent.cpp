// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutSenderComponent.h"

#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "RHICommandList.h"
#include "RenderingThread.h"
#include "Slate/SceneViewport.h"
#include "SpoutSettings.h"
#include "SpoutUE.h"
#include "TextureResource.h"
#include "Widgets/SViewport.h"
#include "Widgets/SWindow.h"

// Slate's present callback stopped passing the back buffer directly and started
// passing this provider somewhere after 5.5 (5.8 has it).
#if __has_include("Slate/SlateViewportProvider.h")
	#include "Slate/SlateViewportProvider.h"
	#define SPOUT_HAS_SLATE_VIEWPORT_PROVIDER 1
#else
	#define SPOUT_HAS_SLATE_VIEWPORT_PROVIDER 0
#endif

namespace SpoutSenderComponentPrivate
{
	FRHITexture* GetBackBuffer(const FTextureRHIRef& BackBuffer)
	{
		return BackBuffer.GetReference();
	}

#if SPOUT_HAS_SLATE_VIEWPORT_PROVIDER
	FRHITexture* GetBackBuffer(ISlateViewportProvider& ViewportProvider)
	{
		return ViewportProvider.GetBackBufferResource();
	}
#endif
}

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
	// Cooking, packaging and other commandlets load levels too, without an RHI.
	if (IsRunningCommandlet())
	{
		return false;
	}

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

	// Editor-world output only flows while the editor keeps ticking its world.
	const UWorld* World = GetWorld();
	if (World && !World->IsGameWorld())
	{
		FSpoutUEModule::NotifyEditorStreamStarted();
		bCountedAsEditorStream = true;
	}

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

	if (bCountedAsEditorStream)
	{
		FSpoutUEModule::NotifyEditorStreamStopped();
		bCountedAsEditorStream = false;
	}

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

void USpoutSenderComponent::BindViewportSource(bool bLogFailures)
{
#if PLATFORM_WINDOWS
	if (BackBufferReadyHandle.IsValid())
	{
		return;
	}

	if (!FSlateApplication::IsInitialized() || !FSlateApplication::Get().GetRenderer())
	{
		UE_CLOG(bLogFailures, LogSpout, Error, TEXT("Sender '%s' cannot use the viewport source: Slate is not available (running headless?)."), *SenderName);
		return;
	}

	if (!GEngine || !GEngine->GameViewport)
	{
		UE_CLOG(bLogFailures, LogSpout, Warning,
			TEXT("Sender '%s' is set to the viewport source but there is no game viewport. This mode works in PIE and packaged builds; use a render target to output from the editor viewport."),
			*SenderName);
		return;
	}

	// In a packaged or -game build the window can arrive a few frames after
	// BeginPlay; the tick keeps trying until it does.
	GameViewportWindow = GEngine->GameViewport->GetWindow();
	if (!GameViewportWindow.IsValid())
	{
		UE_CLOG(bLogFailures, LogSpout, Verbose, TEXT("Sender '%s': the game viewport has no window yet, retrying."), *SenderName);
		return;
	}

	// The second parameter is the back buffer itself on older engines and an
	// ISlateViewportProvider on newer ones; the generic lambda takes either and
	// overload resolution picks the matching GetBackBuffer.
	BackBufferReadyHandle = FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().AddLambda(
		[this](SWindow& Window, auto& BackBufferSource)
		{
			OnBackBufferReady_RenderThread(Window, SpoutSenderComponentPrivate::GetBackBuffer(BackBufferSource));
		});
#endif
}

void USpoutSenderComponent::UnbindViewportSource()
{
#if PLATFORM_WINDOWS
	if (!BackBufferReadyHandle.IsValid())
	{
		return;
	}

	if (FSlateApplication::IsInitialized() && FSlateApplication::Get().GetRenderer())
	{
		FSlateApplication::Get().GetRenderer()->OnBackBufferReadyToPresent().Remove(BackBufferReadyHandle);
	}

	BackBufferReadyHandle.Reset();
	GameViewportWindow.Reset();

	// The delegate fires on the render thread, so an in-flight call has to finish
	// before this component can consider itself detached from it.
	FlushRenderingCommands();
#endif
}

void USpoutSenderComponent::OnBackBufferReady_RenderThread(SWindow& Window, FRHITexture* BackBuffer)
{
#if PLATFORM_WINDOWS
	check(IsInRenderingThread());

	if (!bIsSending || !BackBuffer || !Sender.IsValid())
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
	FIntRect SourceRect;
	{
		FScopeLock Lock(&ViewportSettingsSection);
		Settings.SenderName = ViewportSenderName;
		Settings.PixelFormat = ViewportPixelFormat;
		Settings.ColorSpace = ViewportColorSpace;
		Settings.AlphaMode = ViewportAlphaMode;
		Settings.bFlipVertical = bViewportFlipVertical;
		SourceRect = ViewportSourceRect;
	}

	if (Settings.SenderName.IsEmpty())
	{
		return;
	}

	// The presented frame is already encoded for the display, which is exactly
	// what "match viewport" asks for; encoding it again washes it out.
	if (Settings.ColorSpace == ESpoutColorSpace::MatchViewport)
	{
		Settings.ColorSpace = ESpoutColorSpace::Raw;
	}

	FRHICommandListImmediate& RHICmdList = FRHICommandListExecutor::GetImmediateCommandList();

	const FIntRect FullRect(FIntPoint::ZeroValue, FIntPoint(BackBuffer->GetSizeX(), BackBuffer->GetSizeY()));

	// The window may be resizing under us, so the rectangle from the last tick
	// is clipped to the buffer actually being presented.
	SourceRect.Clip(FullRect);
	if (SourceRect.Area() <= 0)
	{
		SourceRect = FullRect;
	}

	const FIntPoint SourceExtent = SourceRect.Size();

	// The swap chain buffer is not something to sample from or hand to another
	// device, so the presented frame is copied into a texture of our own first.
	if (!ViewportStaging.IsValid()
		|| ViewportStaging->GetSizeX() != SourceExtent.X
		|| ViewportStaging->GetSizeY() != SourceExtent.Y
		|| ViewportStaging->GetFormat() != BackBuffer->GetFormat())
	{
		const FRHITextureCreateDesc Desc =
			FRHITextureCreateDesc::Create2D(TEXT("Spout.ViewportStaging"))
			.SetExtent(SourceExtent)
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
	FRHICopyTextureInfo CopyInfo;
	CopyInfo.SourcePosition = FIntVector(SourceRect.Min.X, SourceRect.Min.Y, 0);
	CopyInfo.Size = FIntVector(SourceExtent.X, SourceExtent.Y, 1);
	RHICmdList.CopyTexture(BackBuffer, ViewportStaging, CopyInfo);

	// Hand the swap chain buffer back in the state the presenter expects.
	RHICmdList.Transition(FRHITransitionInfo(BackBuffer, ERHIAccess::CopySrc, ERHIAccess::Present));

	Sender->Send_RenderThread(RHICmdList, ViewportStaging.GetReference(), Settings);
#endif
}

FIntRect USpoutSenderComponent::ComputeViewportSourceRect() const
{
	const UGameViewportClient* ViewportClient = GEngine ? GEngine->GameViewport : nullptr;
	const FSceneViewport* SceneViewport = ViewportClient ? ViewportClient->GetGameViewport() : nullptr;
	const TSharedPtr<SViewport> ViewportWidget = SceneViewport ? SceneViewport->GetViewportWidget().Pin() : nullptr;
	const TSharedPtr<SWindow> Window = GameViewportWindow.Pin();

	if (!ViewportWidget.IsValid() || !Window.IsValid())
	{
		return FIntRect();
	}

	// Both are in desktop pixels, and the back buffer covers the window from its
	// top left corner, so the difference is the viewport's place in the buffer.
	// The window's geometry is taken from the same Slate pass as the viewport's:
	// its live screen position moves as soon as the OS moves the window, a frame
	// ahead of the cached widget geometry, and mixing the two shifts the
	// rectangle for a frame, which the clip then shrinks into a transient resize
	// of the sender that receivers may not recover from.
	const FGeometry& Geometry = ViewportWidget->GetTickSpaceGeometry();
	const FVector2D Min = FVector2D(Geometry.GetAbsolutePosition()) - FVector2D(Window->GetTickSpaceGeometry().GetAbsolutePosition());
	const FVector2D Max = Min + FVector2D(Geometry.GetAbsoluteSize());

	return FIntRect(
		FIntPoint(FMath::RoundToInt(Min.X), FMath::RoundToInt(Min.Y)),
		FIntPoint(FMath::RoundToInt(Max.X), FMath::RoundToInt(Max.Y)));
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
		if (!BackBufferReadyHandle.IsValid())
		{
			const UWorld* World = GetWorld();
			if (World && World->IsGameWorld())
			{
				BindViewportSource(false);
			}
		}

		// The viewport path is driven by the present callback, so all this tick
		// does is keep the render thread's copy of the settings current.
		const FIntRect SourceRect = ComputeViewportSourceRect();
		{
			FScopeLock Lock(&ViewportSettingsSection);
			ViewportSenderName = SenderName;
			ViewportPixelFormat = PixelFormat;
			ViewportColorSpace = ColorSpace;
			ViewportAlphaMode = AlphaMode;
			bViewportFlipVertical = bFlipVertical;
			ViewportSourceRect = SourceRect;
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
