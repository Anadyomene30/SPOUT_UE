// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutReceiverStream.h"

#if PLATFORM_WINDOWS

#include "SpoutFormatUtils.h"
#include "SpoutUE.h"
#include "RHICommandList.h"

#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include "SpoutDX.h"
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"

namespace
{
	constexpr double SpoutRateWindowSeconds = 1.0;
}

FSpoutReceiverStream::FSpoutReceiverStream() = default;

FSpoutReceiverStream::~FSpoutReceiverStream()
{
	if (Receiver)
	{
		Receiver->ReleaseReceiver();
		Receiver->CloseDirectX11();
		delete Receiver;
		Receiver = nullptr;
	}
}

FSpoutReceiveResult FSpoutReceiverStream::Receive_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* TargetTexture, const FSpoutReceiveParams& Params)
{
	check(IsInRenderingThread());

	const FSpoutReceiveResult Result = ReceiveInternal_RenderThread(RHICmdList, TargetTexture, Params);

	{
		FScopeLock Lock(&StateSection);
		LastResult = Result;
	}

	return Result;
}

FSpoutReceiveResult FSpoutReceiverStream::ReceiveInternal_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* TargetTexture, const FSpoutReceiveParams& Params)
{
	FSpoutReceiveResult Result;

	FSpoutD3D12Device* Device = GetSpoutDevice();
	if (!Device)
	{
		return Result;
	}

	if (!Receiver)
	{
		Receiver = new spoutDX();
		Receiver->OpenDirectX11(Device->GetD3D11Device());
		CurrentName.Reset();
	}

	// An empty name means "follow the active sender", which is exactly what
	// SetReceiverName(nullptr) does.
	if (CurrentName != Params.SenderName)
	{
		// The receiver is in use on the submission thread until this returns.
		RHICmdList.ImmediateFlush(EImmediateFlushType::FlushRHIThread);

		Receiver->ReleaseReceiver();
		CurrentName = Params.SenderName;

		if (CurrentName.IsEmpty())
		{
			Receiver->SetReceiverName();
		}
		else
		{
			Receiver->SetReceiverName(TCHAR_TO_UTF8(*CurrentName));
		}

		// The old surface belongs to the old sender.
		WrappedTexture.SafeRelease();
		WrappedResource.SafeRelease();
		WrappedNativeResource = nullptr;
		ReceiveTexture.SafeRelease();
		CurrentExtent = FIntPoint::ZeroValue;
		CurrentFormat = PF_Unknown;

		FScopeLock Lock(&StateSection);
		QueueState = FQueueState();
	}

	// Spout only runs on the graphics queue (see below), so what the render thread
	// acts on is what the previous trip there found out.
	FQueueState Last;
	{
		FScopeLock Lock(&StateSection);
		Last = QueueState;
		QueueState.bFresh = false;
		QueueState.bUpdated = false;
		QueueState.bNewFrame = false;
	}

	Result.bConnected = Last.bConnected;

	if (!Last.bConnected)
	{
		if (bWasConnected)
		{
			UE_LOG(LogSpout, Log, TEXT("Receiver lost sender '%s'."), *CurrentName);
		}
		bWasConnected = false;

		FScopeLock Lock(&StateSection);
		bConnected = false;
	}
	else
	{
		if (!bWasConnected)
		{
			bWasConnected = true;

			FScopeLock Lock(&StateSection);
			bConnected = true;
			// The first connection is not a reconnection.
			if (Stats.FrameCount > 0)
			{
				Stats.ReconnectCount++;
			}
			Stats.LastError.Reset();
		}

		Result.SenderName = Last.SenderName;
		Result.SenderSize = Last.SenderSize;
		Result.SenderFormat = SpoutUE::FromDXGIFormat(Last.SenderFormat);
		Result.SenderFormatName = SpoutUE::DXGIFormatToString(Last.SenderFormat);

		if (Last.bUpdated)
		{
			Result.bSenderChanged = true;

			if (Result.SenderFormat == PF_Unknown)
			{
				FScopeLock Lock(&StateSection);
				Stats.LastError = FString::Printf(
					TEXT("Sender '%s' publishes %s, which this plugin cannot map to an Unreal pixel format."),
					*Result.SenderName, *Result.SenderFormatName);
			}
			else
			{
				UE_LOG(LogSpout, Log, TEXT("Receiver latched onto '%s' (%dx%d %s)."),
					*Result.SenderName, Result.SenderSize.X, Result.SenderSize.Y, *Result.SenderFormatName);

				EnsureReceiveTexture_RenderThread(RHICmdList, Result.SenderSize, Result.SenderFormat);
			}
		}
		else if (Last.bFresh)
		{
			Result.bNewFrame = Last.bNewFrame;

			if (Result.bNewFrame)
			{
				NoteFrameReceived();
			}
			else
			{
				FScopeLock Lock(&StateSection);
				Stats.SkippedFrames++;
			}
		}
	}

	const bool bHasSurface = EnsureWrappedResource_RenderThread();

	// Runs every frame, connected or not: it is also how a sender is found.
	FSpoutD3D12Device::RunOnGraphicsQueue_RenderThread(RHICmdList,
		[this, Device, Surface = ReceiveTexture, Wrapped = WrappedResource, WrappedView = WrappedTexture, bHasSurface]()
		{
			if (bHasSurface)
			{
				Device->AcquireWrapped(Wrapped.GetReference());
			}

			// ReceiveTexture only reads this pointer; it never allocates on our behalf.
			ID3D11Texture2D* Destination = bHasSurface ? WrappedView.GetReference() : nullptr;
			const bool bIsConnected = Receiver->ReceiveTexture(&Destination);

			if (bHasSurface)
			{
				Device->ReleaseWrapped(Wrapped.GetReference());
			}
			Device->FlushD3D11();

			NoteQueueResult(bIsConnected);
		});

	// Recorded after the copy above, so it reads this frame's pixels. Resolved even
	// on a repeated frame: the target may have just been resized, or may be cleared
	// by whatever else draws to it. Skipped while the surface is being replaced,
	// since the caller resizes its render target this frame and gets pixels the next.
	if (Result.bConnected && !Result.bSenderChanged && TargetTexture && ReceiveTexture.IsValid())
	{
		SpoutUE::ConvertTexture_RenderThread(RHICmdList, ReceiveTexture.GetReference(), TargetTexture, Params.Conversion);
	}

	return Result;
}

void FSpoutReceiverStream::NoteQueueResult(bool bIsConnected)
{
	FQueueState Found;
	Found.bConnected = bIsConnected;

	if (bIsConnected)
	{
		Found.SenderName = UTF8_TO_TCHAR(Receiver->GetSenderName());
		Found.SenderSize = FIntPoint(static_cast<int32>(Receiver->GetSenderWidth()), static_cast<int32>(Receiver->GetSenderHeight()));
		Found.SenderFormat = Receiver->GetSenderFormat();

		// IsUpdated consumes the flag, so it must be called exactly once per receive.
		Found.bUpdated = Receiver->IsUpdated();
		Found.bNewFrame = !Found.bUpdated && Receiver->IsFrameNew();
	}

	FScopeLock Lock(&StateSection);

	// Events survive until the render thread has seen them, in case the queue
	// gets ahead of it by a frame.
	Found.bUpdated |= QueueState.bUpdated;
	Found.bNewFrame |= QueueState.bNewFrame;
	Found.bFresh = true;
	QueueState = MoveTemp(Found);
}

bool FSpoutReceiverStream::EnsureReceiveTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FIntPoint Extent, EPixelFormat Format)
{
	if (Extent.X <= 0 || Extent.Y <= 0 || Format == PF_Unknown)
	{
		return false;
	}

	if (ReceiveTexture.IsValid() && CurrentExtent == Extent && CurrentFormat == Format)
	{
		return true;
	}

	WrappedTexture.SafeRelease();
	WrappedResource.SafeRelease();
	WrappedNativeResource = nullptr;
	ReceiveTexture.SafeRelease();

	const FRHITextureCreateDesc Desc =
		FRHITextureCreateDesc::Create2D(TEXT("Spout.ReceiverSurface"))
		.SetExtent(Extent)
		.SetFormat(Format)
		.SetNumMips(1)
		.SetFlags(ETextureCreateFlags::ShaderResource)
		.SetInitialState(ERHIAccess::SRVMask);

	ReceiveTexture = RHICmdList.CreateTexture(Desc);
	if (!ReceiveTexture.IsValid())
	{
		FScopeLock Lock(&StateSection);
		Stats.LastError = TEXT("Could not create the receiver's surface.");
		return false;
	}

	CurrentExtent = Extent;
	CurrentFormat = Format;
	return true;
}

bool FSpoutReceiverStream::EnsureWrappedResource_RenderThread()
{
	FSpoutD3D12Device* Device = GetSpoutDevice();
	if (!Device || !ReceiveTexture.IsValid())
	{
		return false;
	}

	ID3D12Resource* Native = FSpoutD3D12Device::GetNativeResource(ReceiveTexture.GetReference());
	if (!Native)
	{
		return false;
	}

	if (WrappedResource.IsValid() && Native == WrappedNativeResource)
	{
		return true;
	}

	WrappedTexture.SafeRelease();
	WrappedResource.SafeRelease();

	// A shader-resource-only texture gets no state tracking in the D3D12 RHI: it
	// is assumed to sit in its shader resource state forever and transitions on it
	// are dropped. So that is the state 11On12 finds it in and hands it back in,
	// with 11On12's own barriers around the copy in between.
	constexpr D3D12_RESOURCE_STATES ShaderResourceState = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE | D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE;
	if (!Device->WrapResource(Native, ShaderResourceState, ShaderResourceState, WrappedResource))
	{
		FScopeLock Lock(&StateSection);
		Stats.LastError = TEXT("D3D11On12 could not wrap the receiver's surface.");
		return false;
	}

	if (FAILED(WrappedResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(WrappedTexture.GetInitReference()))))
	{
		WrappedResource.SafeRelease();
		return false;
	}

	WrappedNativeResource = Native;
	return true;
}

void FSpoutReceiverStream::Release_RenderThread()
{
	check(IsInRenderingThread());

	if (Receiver)
	{
		// Receives already handed to the graphics queue still use the receiver.
		FRHICommandListImmediate::Get().ImmediateFlush(EImmediateFlushType::FlushRHIThread);

		Receiver->ReleaseReceiver();
		Receiver->CloseDirectX11();
		delete Receiver;
		Receiver = nullptr;
	}

	WrappedTexture.SafeRelease();
	WrappedResource.SafeRelease();
	WrappedNativeResource = nullptr;
	ReceiveTexture.SafeRelease();

	CurrentExtent = FIntPoint::ZeroValue;
	CurrentFormat = PF_Unknown;
	CurrentName.Reset();
	bWasConnected = false;

	FScopeLock Lock(&StateSection);
	bConnected = false;
	QueueState = FQueueState();
	LastResult = FSpoutReceiveResult();
	Stats.Reset();
	RateWindowStartSeconds = 0.0;
	RateWindowFrames = 0;
}

void FSpoutReceiverStream::NoteFrameReceived()
{
	FScopeLock Lock(&StateSection);

	Stats.FrameCount++;
	RateWindowFrames++;

	const double Now = FPlatformTime::Seconds();
	if (RateWindowStartSeconds <= 0.0)
	{
		RateWindowStartSeconds = Now;
		return;
	}

	const double Elapsed = Now - RateWindowStartSeconds;
	if (Elapsed >= SpoutRateWindowSeconds)
	{
		Stats.FrameRate = static_cast<float>(RateWindowFrames / Elapsed);
		RateWindowStartSeconds = Now;
		RateWindowFrames = 0;
	}
}

FSpoutStreamStats FSpoutReceiverStream::GetStats() const
{
	FScopeLock Lock(&StateSection);
	return Stats;
}

bool FSpoutReceiverStream::IsConnected() const
{
	FScopeLock Lock(&StateSection);
	return bConnected;
}

FSpoutReceiveResult FSpoutReceiverStream::GetLastResult() const
{
	FScopeLock Lock(&StateSection);
	return LastResult;
}

#endif // PLATFORM_WINDOWS
