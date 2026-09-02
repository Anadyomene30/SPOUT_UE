// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "RHIFwd.h"
#include "SpoutUETypes.h"

class FRHICommandListImmediate;
class FSpoutSenderStream;

/** Everything that describes how a texture should be published. */
struct SPOUTUE_API FSpoutSenderSettings
{
	/** Requested name. Spout appends a suffix if the name is already taken. */
	FString SenderName;

	ESpoutPixelFormat PixelFormat = ESpoutPixelFormat::Auto;
	ESpoutColorSpace ColorSpace = ESpoutColorSpace::MatchViewport;
	ESpoutAlphaMode AlphaMode = ESpoutAlphaMode::Opaque;
	bool bFlipVertical = false;
};

/**
 * The public handle onto a Spout sender.
 *
 * Everything that publishes a texture goes through this: the sender component,
 * the media capture, and any C++ of your own. It hides the D3D11On12 machinery
 * and, more importantly, the threading rule that comes with it, which is that
 * the underlying stream may only ever be touched on the render thread.
 *
 * Create one on the game thread, call Send from the render thread, and call
 * Destroy on the game thread when done; Destroy hands teardown over to the
 * render thread rather than doing it inline.
 */
class SPOUTUE_API FSpoutSenderHandle
{
public:
	/** Returns null when the running RHI cannot share Spout textures. */
	static TSharedPtr<FSpoutSenderHandle> Create();

	~FSpoutSenderHandle();

	/** Publishes one frame. Render thread only. */
	void Send_RenderThread(FRHICommandListImmediate& RHICmdList, FRHITexture* SourceTexture, const FSpoutSenderSettings& Settings);

	/**
	 * Tears the sender down.
	 *
	 * Safe to call from the game thread: the GPU resources are released on the
	 * render thread, and the handle is inert afterwards.
	 */
	void Destroy();

	/** Name Spout actually published, empty until the first frame has gone out. */
	FString GetPublishedName() const;

	FIntPoint GetPublishedSize() const;

	FSpoutStreamStats GetStats() const;

	bool IsSending() const;

private:
	FSpoutSenderHandle() = default;

	/** Null once Destroy has been called. */
	FSpoutSenderStream* Stream = nullptr;
};
