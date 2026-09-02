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

namespace
{
	class FSpoutConversionPS : public FGlobalShader
	{
	public:
		DECLARE_GLOBAL_SHADER(FSpoutConversionPS);
		SHADER_USE_PARAMETER_STRUCT(FSpoutConversionPS, FGlobalShader);

		BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
			SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SpoutSourceTexture)
			SHADER_PARAMETER_SAMPLER(SamplerState, SpoutSourceSampler)
			SHADER_PARAMETER(FVector2f, SpoutInvOutputExtent)
			SHADER_PARAMETER(int32, SpoutColorSpaceMode)
			SHADER_PARAMETER(int32, SpoutAlphaMode)
			SHADER_PARAMETER(uint32, SpoutFlipVertical)
			RENDER_TARGET_BINDING_SLOTS()
		END_SHADER_PARAMETER_STRUCT()

		static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
		{
			return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
		}
	};

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

IMPLEMENT_GLOBAL_SHADER(FSpoutConversionPS, "/Plugin/SpoutUE/Private/SpoutConversion.usf", "MainPS", SF_Pixel);

namespace SpoutUE
{
	void ConvertTexture_RenderThread(
		FRHICommandListImmediate& RHICmdList,
		FRHITexture* Source,
		FRHITexture* Destination,
		const FConversionSettings& Settings)
	{
		check(IsInRenderingThread());

		if (!Source || !Destination)
		{
			return;
		}

		if (Settings.IsPassThrough() && CanDirectCopy(Source, Destination))
		{
			RHICmdList.Transition(FRHITransitionInfo(Source, ERHIAccess::Unknown, ERHIAccess::CopySrc));
			RHICmdList.Transition(FRHITransitionInfo(Destination, ERHIAccess::Unknown, ERHIAccess::CopyDest));
			RHICmdList.CopyTexture(Source, Destination, FRHICopyTextureInfo());
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

		GraphBuilder.Execute();
	}
}
