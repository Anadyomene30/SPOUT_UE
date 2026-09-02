// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "SpoutUETypes.h"

class FRDGBuilder;
class FRHICommandListImmediate;
class FRHITexture;

namespace SpoutUE
{
	/** Everything the conversion pass needs to know about a transfer. */
	struct FConversionSettings
	{
		ESpoutColorSpace ColorSpace = ESpoutColorSpace::Raw;
		ESpoutAlphaMode AlphaMode = ESpoutAlphaMode::Opaque;
		bool bFlipVertical = false;

		/** True when the pass would be a plain copy and can be skipped in favour of one. */
		bool IsPassThrough() const
		{
			return ColorSpace == ESpoutColorSpace::Raw
				&& AlphaMode == ESpoutAlphaMode::Straight
				&& !bFlipVertical;
		}
	};

	/**
	 * Resolves Source into Destination, applying the transfer function, alpha
	 * convention and flip described by Settings.
	 *
	 * Falls back to a straight RHI copy when no conversion is needed and the two
	 * textures are copy compatible, so the pass-through case stays free.
	 *
	 * Must be called on the render thread.
	 */
	void ConvertTexture_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		FRHITexture* Source,
		FRHITexture* Destination,
		const FConversionSettings& Settings);

	/**
	 * Composites a straight-alpha overlay (a UMG burn-in) over Destination.
	 *
	 * Must be called on the render thread.
	 */
	void ComposeOverlay_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		FRHITexture* Overlay,
		FRHITexture* Destination);
}
