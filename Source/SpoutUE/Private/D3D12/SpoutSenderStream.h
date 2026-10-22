// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "RHIResources.h"
#include "SpoutConversionShader.h"
#include "SpoutUETypes.h"

#if PLATFORM_WINDOWS

#include "SpoutD3D12Device.h"

class FSpoutSenderDX;
class FRHICommandListImmediate;

/** What a sender should publish, snapshotted on the game thread each frame. */
struct FSpoutSendParams
{
	/** Requested name. Spout appends _1, _2... if another process already owns it. */
	FString SenderName;

	/** Auto follows the source render target. */
	ESpoutPixelFormat PixelFormat = ESpoutPixelFormat::Auto;

	SpoutUE::FConversionSettings Conversion;
};

/**
 * One Spout sender, driven entirely from the render thread.
 *
 * The source texture is first resolved into a texture this object owns. That
 * costs one pass, and buys three things worth more than it: a resource whose
 * lifetime we control (so the D3D11On12 wrapper can be created once instead of
 * per frame), a guaranteed non-typeless format, and somewhere to apply the
 * gamma, alpha and flip conversions.
 *
 * Every method whose name ends in _RenderThread must be called there; the
 * getters are safe from any thread.
 */
class FSpoutSenderStream
{
public:
	FSpoutSenderStream();
	~FSpoutSenderStream();

	/**
	 * Queues one frame for publishing. Returns false if nothing was queued.
	 *
	 * The copy to Spout happens later, on the submission thread, once the frame
	 * has reached the GPU queue; whether Spout accepted it shows up in GetStats.
	 */
	bool Send_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* SourceTexture, const FSpoutSendParams& Params);

	/** Tears the sender down and frees every GPU resource it holds. */
	void Release_RenderThread();

	/** Name Spout actually published, which may differ from the requested one. */
	FString GetPublishedName() const;

	FIntPoint GetPublishedSize() const;

	FSpoutStreamStats GetStats() const;

	bool IsSending() const;

private:
	/** (Re)creates the intermediate texture and drops the wrapper when it changes. */
	bool EnsureSharedTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FIntPoint Extent, EPixelFormat Format);

	/** Creates the D3D11On12 view onto the intermediate texture if it is missing. */
	bool EnsureWrappedResource_RenderThread();

	/** Records the outcome of one publish. Called on the submission thread. */
	void NoteSendResult(bool bSent);

	void NoteFrameSent();

	/** Created on the render thread, then only used on the submission thread until released. */
	FSpoutSenderDX* Sender = nullptr;

	FTextureRHIRef SharedTexture;
	TRefCountPtr<ID3D11Resource> WrappedResource;
	TRefCountPtr<ID3D11Texture2D> WrappedTexture;

	/** Native resource the wrapper was built against, to detect resource churn. */
	ID3D12Resource* WrappedNativeResource = nullptr;

	FIntPoint CurrentExtent = FIntPoint::ZeroValue;
	EPixelFormat CurrentFormat = PF_Unknown;
	FString CurrentName;

	mutable FCriticalSection StateSection;
	FString PublishedName;
	FIntPoint PublishedSize = FIntPoint::ZeroValue;
	FSpoutStreamStats Stats;
	bool bSending = false;

	double RateWindowStartSeconds = 0.0;
	int64 RateWindowFrames = 0;
};

#endif // PLATFORM_WINDOWS
