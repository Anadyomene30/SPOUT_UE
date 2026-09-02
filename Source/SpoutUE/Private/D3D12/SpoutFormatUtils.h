// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "PixelFormat.h"
#include "SpoutUETypes.h"

#if PLATFORM_WINDOWS

#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include <dxgiformat.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"

namespace SpoutUE
{
	/**
	 * The typed DXGI format a sender should advertise for an Unreal pixel format.
	 *
	 * Deliberately not GPixelFormats[..].PlatformFormat: that is typeless for the
	 * formats we care about, and a typeless sender is unusable by receivers.
	 * Returns DXGI_FORMAT_UNKNOWN for formats Spout cannot carry.
	 */
	DXGI_FORMAT ToPublishFormat(EPixelFormat Format);

	/** The Unreal pixel format able to receive a texture of the given DXGI format. */
	EPixelFormat FromDXGIFormat(DXGI_FORMAT Format);

	/** Short human readable name, e.g. "B8G8R8A8_UNORM". */
	FString DXGIFormatToString(DXGI_FORMAT Format);
}

#endif // PLATFORM_WINDOWS
