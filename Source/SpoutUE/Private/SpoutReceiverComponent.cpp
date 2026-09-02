// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutReceiverComponent.h"

#include "Engine/TextureRenderTarget2D.h"
#include "RenderingThread.h"
#include "SpoutSettings.h"
#include "SpoutUE.h"
#include "TextureResource.h"

#if PLATFORM_WINDOWS
#include "D3D12/SpoutReceiverStream.h"
#endif

namespace
{
	/** The render target format that can hold a given received format losslessly. */
	ETextureRenderTargetFormat ToRenderTargetFormat(EPixelFormat Format)
	{
		switch (Format)
		{
		case PF_B8G8R8A8:
		case PF_R8G8B8A8:
		case PF_A8R8G8B8:
			return RTF_RGBA8;

		case PF_A2B10G10R10:
			return RTF_RGB10A2;

		case PF_FloatRGBA:
		case PF_A16B16G16R16:
			return RTF_RGBA16f;

		case PF_A32B32G32R32F:
			return RTF_RGBA32f;

		case PF_FloatR11G11B10:
			return RTF_RG11B10F;

		default:
			return RTF_RGBA8;
		}
	}
}

USpoutReceiverComponent::USpoutReceiverComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
	bTickInEditor = true;

	ColorSpace = USpoutSettings::Get()->DefaultReceiverColorSpace;
}

void USpoutReceiverComponent::OnRegister()
{
	Super::OnRegister();

	if (ShouldAutoStart())
	{
		StartReceiving();
	}
}

void USpoutReceiverComponent::OnUnregister()
{
	StopReceiving();
	Super::OnUnregister();
}

void USpoutReceiverComponent::BeginPlay()
{
	Super::BeginPlay();

	if (ShouldAutoStart())
	{
		StartReceiving();
	}
}

void USpoutReceiverComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopReceiving();
	Super::EndPlay(EndPlayReason);
}

bool USpoutReceiverComponent::ShouldAutoStart() const
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

void USpoutReceiverComponent::StartReceiving()
{
	if (bIsReceiving)
	{
		return;
	}

	if (!FSpoutUEModule::IsRHISupported())
	{
		UE_LOG(LogSpout, Error, TEXT("Receiver cannot start: Spout needs the D3D12 RHI on Windows."));
		return;
	}

	bIsReceiving = true;
	bWasConnected = false;
	ConnectedSender = FSpoutSenderInfo();
}

void USpoutReceiverComponent::StopReceiving()
{
	if (!bIsReceiving && !Stream)
	{
		return;
	}

	const FString PreviousSender = ConnectedSender.Name;
	const bool bWasLive = bWasConnected;

	bIsReceiving = false;
	bWasConnected = false;
	ConnectedSender = FSpoutSenderInfo();
	LastConformedSize = FIntPoint::ZeroValue;
	LastConformedFormat = PF_Unknown;

	DestroyStream();

	if (bWasLive)
	{
		OnDisconnected.Broadcast(PreviousSender);
	}
}

void USpoutReceiverComponent::DestroyStream()
{
#if PLATFORM_WINDOWS
	if (!Stream)
	{
		return;
	}

	FSpoutReceiverStream* Released = Stream;
	Stream = nullptr;

	ENQUEUE_RENDER_COMMAND(SpoutReleaseReceiver)(
		[Released](FRHICommandListImmediate& RHICmdList)
		{
			Released->Release_RenderThread();
			delete Released;
		});
#endif
}

void USpoutReceiverComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIsReceiving)
	{
		return;
	}

	// Act on what the render thread found last frame first, so that the receive
	// enqueued below writes into a render target of the right size.
	ProcessLastResult();
	ReceiveFrame();
}

void USpoutReceiverComponent::ProcessLastResult()
{
#if PLATFORM_WINDOWS
	if (!Stream)
	{
		return;
	}

	const FSpoutReceiveResult Result = Stream->GetLastResult();

	if (!Result.bConnected)
	{
		if (bWasConnected)
		{
			const FString PreviousSender = ConnectedSender.Name;
			bWasConnected = false;
			ConnectedSender = FSpoutSenderInfo();
			OnDisconnected.Broadcast(PreviousSender);
		}
		return;
	}

	const bool bSizeChanged = Result.SenderSize != LastConformedSize || Result.SenderFormat != LastConformedFormat;

	if (Result.SenderSize.X > 0 && Result.SenderSize.Y > 0 && bSizeChanged)
	{
		ConformRenderTarget(Result.SenderSize, Result.SenderFormat);

		if (bWasConnected)
		{
			OnSenderResized.Broadcast(Result.SenderSize);
		}
	}

	ConnectedSender.Name = Result.SenderName;
	ConnectedSender.Width = Result.SenderSize.X;
	ConnectedSender.Height = Result.SenderSize.Y;
	ConnectedSender.Format = Result.SenderFormatName;

	if (!bWasConnected)
	{
		bWasConnected = true;
		UE_LOG(LogSpout, Log, TEXT("Spout receiver connected to '%s' (%dx%d)."),
			*ConnectedSender.Name, ConnectedSender.Width, ConnectedSender.Height);
		OnConnected.Broadcast(ConnectedSender);
	}
#endif
}

void USpoutReceiverComponent::ConformRenderTarget(FIntPoint Size, EPixelFormat Format)
{
	if (Size.X <= 0 || Size.Y <= 0)
	{
		return;
	}

	if (!TargetRenderTarget)
	{
		if (!bAutoCreateRenderTarget)
		{
			return;
		}

		TargetRenderTarget = NewObject<UTextureRenderTarget2D>(this, NAME_None, RF_Transient);
		TargetRenderTarget->RenderTargetFormat = ToRenderTargetFormat(Format);
		TargetRenderTarget->ClearColor = FLinearColor::Black;

		// The conversion pass writes final values, so the hardware must not apply
		// a transfer function of its own on top of them.
		TargetRenderTarget->bForceLinearGamma = (ColorSpace != ESpoutColorSpace::MatchViewport);
		TargetRenderTarget->InitAutoFormat(Size.X, Size.Y);
		TargetRenderTarget->UpdateResourceImmediate(true);
	}
	else if (bAutoResizeRenderTarget)
	{
		if (TargetRenderTarget->SizeX != Size.X || TargetRenderTarget->SizeY != Size.Y)
		{
			TargetRenderTarget->ResizeTarget(Size.X, Size.Y);
		}
	}

	LastConformedSize = Size;
	LastConformedFormat = Format;
}

void USpoutReceiverComponent::ReceiveFrame()
{
#if PLATFORM_WINDOWS
	if (!Stream)
	{
		Stream = new FSpoutReceiverStream();
	}

	FSpoutReceiveParams Params;
	Params.SenderName = SenderName;
	Params.Conversion.ColorSpace = ColorSpace;
	Params.Conversion.AlphaMode = AlphaMode;
	Params.Conversion.bFlipVertical = bFlipVertical;

	// A null resource is expected on the first frames: the receiver still needs to
	// run so it can discover the sender and report the size to conform to.
	FTextureRenderTargetResource* Resource = TargetRenderTarget
		? TargetRenderTarget->GameThread_GetRenderTargetResource()
		: nullptr;

	FSpoutReceiverStream* StreamPtr = Stream;

	ENQUEUE_RENDER_COMMAND(SpoutReceiveFrame)(
		[StreamPtr, Resource, Params](FRHICommandListImmediate& RHICmdList)
		{
			FRHITexture* TargetTexture = nullptr;
			if (Resource)
			{
				TargetTexture = Resource->GetRenderTargetTexture();
				if (!TargetTexture)
				{
					TargetTexture = Resource->TextureRHI;
				}
			}

			StreamPtr->Receive_RenderThread(RHICmdList, TargetTexture, Params);
		});
#endif
}

bool USpoutReceiverComponent::IsConnected() const
{
#if PLATFORM_WINDOWS
	return bIsReceiving && Stream && Stream->IsConnected();
#else
	return false;
#endif
}

void USpoutReceiverComponent::SetSenderName(const FString& NewName)
{
	if (NewName == SenderName)
	{
		return;
	}

	SenderName = NewName;

	if (bIsReceiving)
	{
		StopReceiving();
		StartReceiving();
	}
}

FSpoutSenderInfo USpoutReceiverComponent::GetConnectedSenderInfo() const
{
	return ConnectedSender;
}

FSpoutStreamStats USpoutReceiverComponent::GetStats() const
{
#if PLATFORM_WINDOWS
	return Stream ? Stream->GetStats() : FSpoutStreamStats();
#else
	return FSpoutStreamStats();
#endif
}

#if WITH_EDITOR
void USpoutReceiverComponent::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	const FName PropertyName = PropertyChangedEvent.GetPropertyName();

	if (PropertyName == GET_MEMBER_NAME_CHECKED(USpoutReceiverComponent, SenderName)
		|| PropertyName == GET_MEMBER_NAME_CHECKED(USpoutReceiverComponent, StartupPolicy))
	{
		StopReceiving();

		if (ShouldAutoStart())
		{
			StartReceiving();
		}
	}

	Super::PostEditChangeProperty(PropertyChangedEvent);
}
#endif
