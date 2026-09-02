// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "RHIResources.h"
#include "SpoutConversionShader.h"
#include "SpoutUETypes.h"

#if PLATFORM_WINDOWS

#include "SpoutD3D12Device.h"

class spoutDX;
class FRHICommandListImmediate;

/** What a receiver should latch onto, snapshotted on the game thread each frame. */
struct FSpoutReceiveParams
{
	/** Sender to connect to. Empty means "whatever Spout considers the active sender". */
	FString SenderName;

	SpoutUE::FConversionSettings Conversion;
};

/** What the render thread learned about the connection, read back on the game thread. */
struct FSpoutReceiveResult
{
	bool bConnected = false;
	bool bNewFrame = false;

	/** Set on the frame the sender appeared or changed size or format. */
	bool bSenderChanged = false;

	FString SenderName;
	FIntPoint SenderSize = FIntPoint::ZeroValue;
	EPixelFormat SenderFormat = PF_Unknown;
	FString SenderFormatName;
};

/**
 * One Spout receiver, driven entirely from the render thread.
 *
 * Spout copies the sender's shared texture straight into a D3D11 texture we
 * supply. That texture is a D3D11On12 view onto a D3D12 texture this object
 * owns, so the sender's pixels land in Unreal memory with a single GPU copy;
 * a second pass then resolves them into the caller's render target, applying
 * the transfer function and any rescaling.
 */
class FSpoutReceiverStream
{
public:
	FSpoutReceiverStream();
	~FSpoutReceiverStream();

	/**
	 * Pulls the latest frame into TargetTexture.
	 *
	 * TargetTexture may be null on the frames where the caller has not built its
	 * render target yet: the connection is still established and the result still
	 * reports the sender's size, which is what the caller needs to size it.
	 */
	FSpoutReceiveResult Receive_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* TargetTexture, const FSpoutReceiveParams& Params);

	void Release_RenderThread();

	FSpoutStreamStats GetStats() const;

	bool IsConnected() const;

	/**
	 * The outcome of the most recent render-thread receive.
	 *
	 * The game thread reads this on the following tick to resize its render
	 * target and fire connection events; there is no synchronous path back from
	 * the render thread, and blocking on one would defeat the point.
	 */
	FSpoutReceiveResult GetLastResult() const;

private:
	FSpoutReceiveResult ReceiveInternal_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* TargetTexture, const FSpoutReceiveParams& Params);

	bool EnsureReceiveTexture_RenderThread(FRHICommandListImmediate& RHICmdList, FIntPoint Extent, EPixelFormat Format);
	bool EnsureWrappedResource_RenderThread();
	void NoteFrameReceived();

	spoutDX* Receiver = nullptr;

	FTextureRHIRef ReceiveTexture;
	TRefCountPtr<ID3D11Resource> WrappedResource;
	TRefCountPtr<ID3D11Texture2D> WrappedTexture;
	ID3D12Resource* WrappedNativeResource = nullptr;

	FIntPoint CurrentExtent = FIntPoint::ZeroValue;
	EPixelFormat CurrentFormat = PF_Unknown;
	FString CurrentName;
	bool bWasConnected = false;

	mutable FCriticalSection StateSection;
	FSpoutStreamStats Stats;
	FSpoutReceiveResult LastResult;
	bool bConnected = false;

	double RateWindowStartSeconds = 0.0;
	int64 RateWindowFrames = 0;
};

#endif // PLATFORM_WINDOWS
