// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutSenderHandle.h"

#include "RenderingThread.h"
#include "SpoutUE.h"

#if PLATFORM_WINDOWS
#include "D3D12/SpoutSenderStream.h"
#endif

TSharedPtr<FSpoutSenderHandle> FSpoutSenderHandle::Create()
{
#if PLATFORM_WINDOWS
	if (!FSpoutUEModule::IsRHISupported())
	{
		return nullptr;
	}

	TSharedPtr<FSpoutSenderHandle> Handle = MakeShareable(new FSpoutSenderHandle());
	Handle->Stream = new FSpoutSenderStream();
	return Handle;
#else
	return nullptr;
#endif
}

FSpoutSenderHandle::~FSpoutSenderHandle()
{
	Destroy();
}

void FSpoutSenderHandle::Destroy()
{
#if PLATFORM_WINDOWS
	if (!Stream)
	{
		return;
	}

	FSpoutSenderStream* Released = Stream;
	Stream = nullptr;

	ENQUEUE_RENDER_COMMAND(SpoutDestroySender)(
		[Released](FRHICommandListImmediate& RHICmdList)
		{
			Released->Release_RenderThread();
			delete Released;
		});
#endif
}

void FSpoutSenderHandle::Send_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* SourceTexture, const FSpoutSenderSettings& Settings)
{
#if PLATFORM_WINDOWS
	if (!Stream)
	{
		return;
	}

	FSpoutSendParams Params;
	Params.SenderName = Settings.SenderName;
	Params.PixelFormat = Settings.PixelFormat;
	Params.Conversion.ColorSpace = Settings.ColorSpace;
	Params.Conversion.AlphaMode = Settings.AlphaMode;
	Params.Conversion.bFlipVertical = Settings.bFlipVertical;

	Stream->Send_RenderThread(RHICmdList, SourceTexture, Params);
#endif
}

FString FSpoutSenderHandle::GetPublishedName() const
{
#if PLATFORM_WINDOWS
	return Stream ? Stream->GetPublishedName() : FString();
#else
	return FString();
#endif
}

FIntPoint FSpoutSenderHandle::GetPublishedSize() const
{
#if PLATFORM_WINDOWS
	return Stream ? Stream->GetPublishedSize() : FIntPoint::ZeroValue;
#else
	return FIntPoint::ZeroValue;
#endif
}

FSpoutStreamStats FSpoutSenderHandle::GetStats() const
{
#if PLATFORM_WINDOWS
	return Stream ? Stream->GetStats() : FSpoutStreamStats();
#else
	return FSpoutStreamStats();
#endif
}

bool FSpoutSenderHandle::IsSending() const
{
#if PLATFORM_WINDOWS
	return Stream && Stream->IsSending();
#else
	return false;
#endif
}
