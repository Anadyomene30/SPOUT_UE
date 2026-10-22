// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutSenderStream.h"

#if PLATFORM_WINDOWS

#include "SpoutFormatUtils.h"
#include "SpoutUE.h"
#include "RHICommandList.h"

#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include "SpoutUEExtensions.h"
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"

namespace
{
	/** Rate statistics are recomputed over a one second window. */
	constexpr double SpoutRateWindowSeconds = 1.0;
}

FSpoutSenderStream::FSpoutSenderStream() = default;

FSpoutSenderStream::~FSpoutSenderStream()
{
	// Release_RenderThread is the supported teardown path; this only catches the
	// case of a stream dropped without one, which would otherwise leak the sender
	// name in Spout's shared memory for the lifetime of the process.
	if (Sender)
	{
		Sender->ReleaseSender();
		Sender->CloseDirectX11();
		delete Sender;
		Sender = nullptr;
	}
}

bool FSpoutSenderStream::Send_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* SourceTexture, const FSpoutSendParams& Params)
{
	check(IsInRenderingThread());

	if (!SourceTexture || Params.SenderName.IsEmpty())
	{
		return false;
	}

	FSpoutD3D12Device* Device = GetSpoutDevice();
	if (!Device)
	{
		return false;
	}

	const FIntPoint SourceExtent(SourceTexture->GetSizeX(), SourceTexture->GetSizeY());
	if (SourceExtent.X <= 0 || SourceExtent.Y <= 0)
	{
		return false;
	}

	const EPixelFormat TargetFormat = SpoutUE::ToPixelFormat(Params.PixelFormat, SourceTexture->GetFormat());
	const DXGI_FORMAT PublishFormat = SpoutUE::ToPublishFormat(TargetFormat);
	if (PublishFormat == DXGI_FORMAT_UNKNOWN)
	{
		FScopeLock Lock(&StateSection);
		Stats.LastError = FString::Printf(
			TEXT("Pixel format %s cannot be shared over Spout. Pick an explicit format on the sender."),
			GetPixelFormatString(TargetFormat));
		return false;
	}

	// A rename means a different sender as far as Spout is concerned.
	if (CurrentName != Params.SenderName)
	{
		Release_RenderThread();
		CurrentName = Params.SenderName;
	}

	if (!EnsureSharedTexture_RenderThread(RHICmdList, SourceExtent, TargetFormat))
	{
		return false;
	}

	// Always ends on a raster write, so the surface is a render target as far as
	// Unreal is concerned, which is the state the wrapper is declared with.
	SpoutUE::ConvertTexture_RenderThread(RHICmdList, SourceTexture, SharedTexture.GetReference(), Params.Conversion, ERHIAccess::RTV);

	if (!EnsureWrappedResource_RenderThread())
	{
		return false;
	}

	if (!Sender)
	{
		Sender = new FSpoutSenderDX();
		Sender->OpenDirectX11(Device->GetD3D11Device());
		Sender->SetSenderName(TCHAR_TO_UTF8(*CurrentName));
	}

	// The references keep the surface alive if the stream resizes before this
	// runs; Sender is only deleted after an RHI thread flush.
	FSpoutD3D12Device::RunOnGraphicsQueue_RenderThread(RHICmdList,
		[this, Device, Surface = SharedTexture, Wrapped = WrappedResource, WrappedView = WrappedTexture, PublishFormat]()
		{
			Device->AcquireWrapped(Wrapped.GetReference());
			const bool bSent = Sender->SendTextureAs(WrappedView.GetReference(), PublishFormat);
			Device->ReleaseWrapped(Wrapped.GetReference());
			Device->FlushD3D11();

			NoteSendResult(bSent);
		});

	return true;
}

void FSpoutSenderStream::NoteSendResult(bool bSent)
{
	{
		FScopeLock Lock(&StateSection);
		if (bSent)
		{
			bSending = true;
			PublishedName = UTF8_TO_TCHAR(Sender->GetName());
			PublishedSize = FIntPoint(static_cast<int32>(Sender->GetWidth()), static_cast<int32>(Sender->GetHeight()));
			Stats.LastError.Reset();
		}
		else
		{
			// The sender mutex was held elsewhere. Dropping the frame is the
			// correct behaviour, so this is counted, not reported as an error.
			Stats.SkippedFrames++;
		}
	}

	if (bSent)
	{
		NoteFrameSent();
	}
}

bool FSpoutSenderStream::EnsureSharedTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FIntPoint Extent, EPixelFormat Format)
{
	if (SharedTexture.IsValid() && CurrentExtent == Extent && CurrentFormat == Format)
	{
		return true;
	}

	// Any wrapper points at the old resource.
	WrappedTexture.SafeRelease();
	WrappedResource.SafeRelease();
	WrappedNativeResource = nullptr;
	SharedTexture.SafeRelease();

	const FRHITextureCreateDesc Desc =
		FRHITextureCreateDesc::Create2D(TEXT("Spout.SenderSurface"))
		.SetExtent(Extent)
		.SetFormat(Format)
		.SetNumMips(1)
		.SetFlags(ETextureCreateFlags::ShaderResource | ETextureCreateFlags::RenderTargetable)
		.SetInitialState(ERHIAccess::SRVMask);

	SharedTexture = RHICmdList.CreateTexture(Desc);
	if (!SharedTexture.IsValid())
	{
		FScopeLock Lock(&StateSection);
		Stats.LastError = TEXT("Could not create the sender's intermediate texture.");
		return false;
	}

	CurrentExtent = Extent;
	CurrentFormat = Format;

	UE_LOG(LogSpout, Verbose, TEXT("Sender '%s' surface is now %dx%d (%s)."),
		*CurrentName, Extent.X, Extent.Y, GetPixelFormatString(Format));

	return true;
}

bool FSpoutSenderStream::EnsureWrappedResource_RenderThread()
{
	FSpoutD3D12Device* Device = GetSpoutDevice();
	if (!Device || !SharedTexture.IsValid())
	{
		return false;
	}

	ID3D12Resource* Native = FSpoutD3D12Device::GetNativeResource(SharedTexture.GetReference());
	if (!Native)
	{
		FScopeLock Lock(&StateSection);
		Stats.LastError = TEXT("The intermediate texture has no D3D12 resource behind it.");
		return false;
	}

	if (WrappedResource.IsValid() && Native == WrappedNativeResource)
	{
		return true;
	}

	WrappedTexture.SafeRelease();
	WrappedResource.SafeRelease();

	// Unreal believes the surface stays a render target between conversions, so
	// that is what 11On12 finds it in and hands it back in; 11On12 issues the
	// barriers around its own copy. Transitioning it on the Unreal side instead
	// does not work: the D3D12 RHI drops that transition (verified on 5.5 with
	// the debug layer), and the copy then reads a resource still in RENDER_TARGET.
	if (!Device->WrapResource(Native, D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_RENDER_TARGET, WrappedResource))
	{
		FScopeLock Lock(&StateSection);
		Stats.LastError = TEXT("D3D11On12 could not wrap the sender's texture.");
		return false;
	}

	if (FAILED(WrappedResource->QueryInterface(__uuidof(ID3D11Texture2D), reinterpret_cast<void**>(WrappedTexture.GetInitReference()))))
	{
		WrappedResource.SafeRelease();
		FScopeLock Lock(&StateSection);
		Stats.LastError = TEXT("The wrapped sender resource is not a 2D texture.");
		return false;
	}

	WrappedNativeResource = Native;
	return true;
}

void FSpoutSenderStream::Release_RenderThread()
{
	check(IsInRenderingThread());

	if (Sender)
	{
		// Frames already handed to the graphics queue still use the sender.
		FRHICommandListImmediate::Get().ImmediateFlush(EImmediateFlushType::FlushRHIThread);

		Sender->ReleaseSender();
		Sender->CloseDirectX11();
		delete Sender;
		Sender = nullptr;
	}

	WrappedTexture.SafeRelease();
	WrappedResource.SafeRelease();
	WrappedNativeResource = nullptr;
	SharedTexture.SafeRelease();

	CurrentExtent = FIntPoint::ZeroValue;
	CurrentFormat = PF_Unknown;
	CurrentName.Reset();

	FScopeLock Lock(&StateSection);
	bSending = false;
	PublishedName.Reset();
	PublishedSize = FIntPoint::ZeroValue;
	Stats.Reset();
	RateWindowStartSeconds = 0.0;
	RateWindowFrames = 0;
}

void FSpoutSenderStream::NoteFrameSent()
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

FString FSpoutSenderStream::GetPublishedName() const
{
	FScopeLock Lock(&StateSection);
	return PublishedName;
}

FIntPoint FSpoutSenderStream::GetPublishedSize() const
{
	FScopeLock Lock(&StateSection);
	return PublishedSize;
}

FSpoutStreamStats FSpoutSenderStream::GetStats() const
{
	FScopeLock Lock(&StateSection);
	return Stats;
}

bool FSpoutSenderStream::IsSending() const
{
	FScopeLock Lock(&StateSection);
	return bSending;
}

#endif // PLATFORM_WINDOWS
