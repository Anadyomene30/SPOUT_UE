// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "GlobalShader.h"
#include "ShaderParameterStruct.h"

/**
 * The plugin's global shaders, in a module of their own.
 *
 * Global shader types have to exist before the engine builds its global shader
 * map, which means a module loaded at PostConfigInit. SpoutUE itself carries
 * UObjects and cannot load that early, so the shader types and the shader
 * directory mapping live here and SpoutUE only uses them.
 */

/** Resolves a source texture into a destination with gamma, alpha and flip conversions. */
class FSpoutConversionPS : public FGlobalShader
{
public:
	DECLARE_EXPORTED_GLOBAL_SHADER(FSpoutConversionPS, SPOUTUESHADERS_API);
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

/** Composites a straight-alpha overlay (a UMG burn-in) over a capture. */
class FSpoutOverlayPS : public FGlobalShader
{
public:
	DECLARE_EXPORTED_GLOBAL_SHADER(FSpoutOverlayPS, SPOUTUESHADERS_API);
	SHADER_USE_PARAMETER_STRUCT(FSpoutOverlayPS, FGlobalShader);

	BEGIN_SHADER_PARAMETER_STRUCT(FParameters, )
		SHADER_PARAMETER_RDG_TEXTURE(Texture2D, SpoutOverlayTexture)
		SHADER_PARAMETER_SAMPLER(SamplerState, SpoutOverlaySampler)
		SHADER_PARAMETER(FVector2f, SpoutOverlayInvExtent)
		RENDER_TARGET_BINDING_SLOTS()
	END_SHADER_PARAMETER_STRUCT()

	static bool ShouldCompilePermutation(const FGlobalShaderPermutationParameters& Parameters)
	{
		return IsFeatureLevelSupported(Parameters.Platform, ERHIFeatureLevel::SM5);
	}
};
