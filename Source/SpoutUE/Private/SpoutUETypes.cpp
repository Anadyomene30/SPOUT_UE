// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutUETypes.h"

namespace SpoutUE
{
	bool GetPresetResolution(ESpoutResolution Preset, int32& OutWidth, int32& OutHeight)
	{
		switch (Preset)
		{
		case ESpoutResolution::R240p:	OutWidth = 426;  OutHeight = 240;  return true;
		case ESpoutResolution::R360p:	OutWidth = 640;  OutHeight = 360;  return true;
		case ESpoutResolution::R480p:	OutWidth = 854;  OutHeight = 480;  return true;
		case ESpoutResolution::R720p:	OutWidth = 1280; OutHeight = 720;  return true;
		case ESpoutResolution::R1080p:	OutWidth = 1920; OutHeight = 1080; return true;
		case ESpoutResolution::R1440p:	OutWidth = 2560; OutHeight = 1440; return true;
		case ESpoutResolution::R2160p:	OutWidth = 3840; OutHeight = 2160; return true;
		case ESpoutResolution::Custom:
		default:
			return false;
		}
	}

	EPixelFormat ToPixelFormat(ESpoutPixelFormat Format, EPixelFormat FallbackForAuto)
	{
		switch (Format)
		{
		case ESpoutPixelFormat::BGRA8:		return PF_B8G8R8A8;
		case ESpoutPixelFormat::RGBA8:		return PF_R8G8B8A8;
		case ESpoutPixelFormat::RGB10A2:	return PF_A2B10G10R10;
		case ESpoutPixelFormat::RGBA16F:	return PF_FloatRGBA;
		case ESpoutPixelFormat::RGBA32F:	return PF_A32B32G32R32F;
		case ESpoutPixelFormat::Auto:
		default:							return FallbackForAuto;
		}
	}

	bool IsHighPrecision(EPixelFormat Format)
	{
		switch (Format)
		{
		case PF_FloatRGBA:
		case PF_A32B32G32R32F:
		case PF_A16B16G16R16:
		case PF_A2B10G10R10:
		case PF_FloatR11G11B10:
			return true;
		default:
			return false;
		}
	}
}
