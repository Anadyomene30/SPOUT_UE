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

	FSpoutD3D12Device* Device = FSpoutUEModule::Get().GetDevice();
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
	}

	const bool bHasSurface = EnsureWrappedResource_RenderThread();

	if (bHasSurface)
	{
		// Spout writes into our texture from the D3D11 side, so Unreal has to be
		// done with it and it has to be in the state the wrapper was declared with.
		RHICmdList.Transition(FRHITransitionInfo(ReceiveTexture.GetReference(), ERHIAccess::Unknown, ERHIAccess::CopyDest));
		RHICmdList.SubmitCommandsHint();
		Device->AcquireWrapped(WrappedResource.GetReference());
	}

	// ReceiveTexture only reads this pointer; it never allocates on our behalf.
	ID3D11Texture2D* Destination = bHasSurface ? WrappedTexture.GetReference() : nullptr;
	const bool bIsConnected = Receiver->ReceiveTexture(&Destination);

	if (bHasSurface)
	{
		Device->ReleaseWrapped(WrappedResource.GetReference());
		Device->FlushD3D11();
	}

	Result.bConnected = bIsConnected;

	if (!bIsConnected)
	{
		if (bWasConnected)
		{
			UE_LOG(LogSpout, Log, TEXT("Receiver lost sender '%s'."), *CurrentName);
		}
		bWasConnected = false;

		FScopeLock Lock(&StateSection);
		bConnected = false;
		return Result;
	}

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

	Result.SenderName = UTF8_TO_TCHAR(Receiver->GetSenderName());
	Result.SenderSize = FIntPoint(static_cast<int32>(Receiver->GetSenderWidth()), static_cast<int32>(Receiver->GetSenderHeight()));

	const DXGI_FORMAT SenderDXGIFormat = Receiver->GetSenderFormat();
	Result.SenderFormat = SpoutUE::FromDXGIFormat(SenderDXGIFormat);
	Result.SenderFormatName = SpoutUE::DXGIFormatToString(SenderDXGIFormat);

	// IsUpdated consumes the flag, so it must be called exactly once per frame.
	if (Receiver->IsUpdated())
	{
		Result.bSenderChanged = true;

		if (Result.SenderFormat == PF_Unknown)
		{
			FScopeLock Lock(&StateSection);
			Stats.LastError = FString::Printf(
				TEXT("Sender '%s' publishes %s, which this plugin cannot map to an Unreal pixel format."),
				*Result.SenderName, *Result.SenderFormatName);
			return Result;
		}

		UE_LOG(LogSpout, Log, TEXT("Receiver latched onto '%s' (%dx%d %s)."),
			*Result.SenderName, Result.SenderSize.X, Result.SenderSize.Y, *Result.SenderFormatName);

		EnsureReceiveTexture_RenderThread(RHICmdList, Result.SenderSize, Result.SenderFormat);

		// The surface was only just created, so there is no frame in it yet.
		// The caller resizes its render target this frame and gets pixels the next.
		return Result;
	}

	Result.bNewFrame = Receiver->IsFrameNew();

	if (Result.bNewFrame)
	{
		NoteFrameReceived();
	}
	else
	{
		FScopeLock Lock(&StateSection);
		Stats.SkippedFrames++;
	}

	// Resolve into the caller's render target even on a repeated frame: the target
	// may have just been resized, or may be cleared by whatever else draws to it.
	if (TargetTexture && ReceiveTexture.IsValid())
	{
		SpoutUE::ConvertTexture_RenderThread(RHICmdList, ReceiveTexture.GetReference(), TargetTexture, Params.Conversion);
	}

	return Result;
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
		.SetInitialState(ERHIAccess::CopyDest);

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
	FSpoutD3D12Device* Device = FSpoutUEModule::Get().GetDevice();
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

	if (!Device->WrapResource(Native, D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_COPY_DEST, WrappedResource))
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
