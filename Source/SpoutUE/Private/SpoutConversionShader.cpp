// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutConversionShader.h"

#include "CommonRenderResources.h"
#include "GlobalShader.h"
#include "PixelShaderUtils.h"
#include "RHICommandList.h"
#include "RHIResources.h"
#include "RenderGraphBuilder.h"
#include "RenderGraphUtils.h"
#include "ScreenPass.h"
#include "ShaderParameterStruct.h"
#include "SpoutUEShaders.h"

namespace
{
	int32 ToShaderColorSpace(ESpoutColorSpace ColorSpace)
	{
		switch (ColorSpace)
		{
		case ESpoutColorSpace::MatchViewport:	return 1;
		case ESpoutColorSpace::ToLinear:		return 2;
		case ESpoutColorSpace::Raw:
		default:								return 0;
		}
	}

	int32 ToShaderAlphaMode(ESpoutAlphaMode AlphaMode)
	{
		switch (AlphaMode)
		{
		case ESpoutAlphaMode::Straight:				return 1;
		case ESpoutAlphaMode::UnpremultiplyAlpha:	return 2;
		case ESpoutAlphaMode::PremultiplyAlpha:		return 3;
		case ESpoutAlphaMode::Opaque:
		default:									return 0;
		}
	}

	/** CopyTexture needs matching extents and a compatible format family. */
	bool CanDirectCopy(FRHITexture* Source, FRHITexture* Destination)
	{
		const FIntVector SourceSize = Source->GetSizeXYZ();
		const FIntVector DestinationSize = Destination->GetSizeXYZ();

		return SourceSize == DestinationSize
			&& Source->GetFormat() == Destination->GetFormat();
	}
}

namespace SpoutUE
{
	void ConvertTexture_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		FRHITexture* Source,
		FRHITexture* Destination,
		const FConversionSettings& Settings,
		ERHIAccess DestinationAccess)
	{
		check(IsInRenderingThread());

		if (!Source || !Destination)
		{
			return;
		}

		// A plain copy would leave Destination in CopyDest, so a caller that needs
		// a render target at the end always gets the pass.
		if (Settings.IsPassThrough() && DestinationAccess != ERHIAccess::RTV && CanDirectCopy(Source, Destination))
		{
			RHICmdList.Transition(FRHITransitionInfo(Source, ERHIAccess::Unknown, ERHIAccess::CopySrc));
			RHICmdList.Transition(FRHITransitionInfo(Destination, ERHIAccess::Unknown, ERHIAccess::CopyDest));
			RHICmdList.CopyTexture(Source, Destination, FRHICopyTextureInfo());
			RHICmdList.Transition(FRHITransitionInfo(Destination, ERHIAccess::CopyDest, DestinationAccess));
			return;
		}

		const FIntPoint DestinationExtent(Destination->GetSizeX(), Destination->GetSizeY());
		if (DestinationExtent.X <= 0 || DestinationExtent.Y <= 0)
		{
			return;
		}

		FRDGBuilder GraphBuilder(RHICmdList);

		FRDGTextureRef SourceRDG = RegisterExternalTexture(GraphBuilder, Source, TEXT("Spout.ConversionSource"));
		FRDGTextureRef DestinationRDG = RegisterExternalTexture(GraphBuilder, Destination, TEXT("Spout.ConversionDest"));

		FSpoutConversionPS::FParameters* Parameters = GraphBuilder.AllocParameters<FSpoutConversionPS::FParameters>();
		Parameters->SpoutSourceTexture = SourceRDG;
		Parameters->SpoutSourceSampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->SpoutInvOutputExtent = FVector2f(1.f / DestinationExtent.X, 1.f / DestinationExtent.Y);
		Parameters->SpoutColorSpaceMode = ToShaderColorSpace(Settings.ColorSpace);
		Parameters->SpoutAlphaMode = ToShaderAlphaMode(Settings.AlphaMode);
		Parameters->SpoutFlipVertical = Settings.bFlipVertical ? 1u : 0u;
		Parameters->RenderTargets[0] = FRenderTargetBinding(DestinationRDG, ERenderTargetLoadAction::ENoAction);

		const TShaderMapRef<FSpoutConversionPS> PixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			GetGlobalShaderMap(GMaxRHIFeatureLevel),
			RDG_EVENT_NAME("Spout.Convert %dx%d", DestinationExtent.X, DestinationExtent.Y),
			PixelShader,
			Parameters,
			FIntRect(0, 0, DestinationExtent.X, DestinationExtent.Y));

		GraphBuilder.SetTextureAccessFinal(DestinationRDG, DestinationAccess);
		GraphBuilder.Execute();
	}

	void ComposeOverlay_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		FRHITexture* Overlay,
		FRHITexture* Destination)
	{
		check(IsInRenderingThread());

		if (!Overlay || !Destination)
		{
			return;
		}

		const FIntPoint DestinationExtent(Destination->GetSizeX(), Destination->GetSizeY());
		if (DestinationExtent.X <= 0 || DestinationExtent.Y <= 0)
		{
			return;
		}

		FRDGBuilder GraphBuilder(RHICmdList);

		FRDGTextureRef OverlayRDG = RegisterExternalTexture(GraphBuilder, Overlay, TEXT("Spout.Overlay"));
		FRDGTextureRef DestinationRDG = RegisterExternalTexture(GraphBuilder, Destination, TEXT("Spout.OverlayDest"));

		FSpoutOverlayPS::FParameters* Parameters = GraphBuilder.AllocParameters<FSpoutOverlayPS::FParameters>();
		Parameters->SpoutOverlayTexture = OverlayRDG;
		Parameters->SpoutOverlaySampler = TStaticSamplerState<SF_Bilinear, AM_Clamp, AM_Clamp, AM_Clamp>::GetRHI();
		Parameters->SpoutOverlayInvExtent = FVector2f(1.f / DestinationExtent.X, 1.f / DestinationExtent.Y);

		// Load, not clear: the capture underneath is what the overlay lands on.
		Parameters->RenderTargets[0] = FRenderTargetBinding(DestinationRDG, ERenderTargetLoadAction::ELoad);

		const TShaderMapRef<FSpoutOverlayPS> PixelShader(GetGlobalShaderMap(GMaxRHIFeatureLevel));

		FPixelShaderUtils::AddFullscreenPass(
			GraphBuilder,
			GetGlobalShaderMap(GMaxRHIFeatureLevel),
			RDG_EVENT_NAME("Spout.ComposeOverlay"),
			PixelShader,
			Parameters,
			FIntRect(0, 0, DestinationExtent.X, DestinationExtent.Y),
			// Straight-alpha "over": UMG output is not premultiplied.
			TStaticBlendState<CW_RGBA, BO_Add, BF_SourceAlpha, BF_InverseSourceAlpha, BO_Add, BF_One, BF_InverseSourceAlpha>::GetRHI());

		GraphBuilder.Execute();
	}
}
