// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "PixelFormat.h"
#include "SpoutUETypes.generated.h"

/** When a sender or receiver should bring itself up without being told to. */
UENUM(BlueprintType)
enum class ESpoutStartupPolicy : uint8
{
	/** Never starts on its own. Call Start() from Blueprint or C++. */
	Manual				UMETA(DisplayName = "Manual"),

	/** Starts in PIE and in packaged/standalone builds, but stays idle in the editor viewport. */
	GameOnly			UMETA(DisplayName = "Game only (PIE + packaged)"),

	/** Starts in the editor viewport as well as in game. Handy for live editing sessions. */
	EditorAndGame		UMETA(DisplayName = "Editor and game"),
};

/** How the alpha channel of the shared texture should be interpreted. */
UENUM(BlueprintType)
enum class ESpoutAlphaMode : uint8
{
	/** Alpha is forced to 1. Cheapest, and what most VJ software expects from an opaque feed. */
	Opaque					UMETA(DisplayName = "Opaque (force alpha = 1)"),

	/** Alpha is passed through untouched. */
	Straight				UMETA(DisplayName = "Straight (pass through)"),

	/** Colour is divided by alpha on the way out. Use when Unreal hands you premultiplied pixels. */
	UnpremultiplyAlpha		UMETA(DisplayName = "Un-premultiply (premultiplied -> straight)"),

	/** Colour is multiplied by alpha on the way out. Use when the receiver expects premultiplied pixels. */
	PremultiplyAlpha		UMETA(DisplayName = "Premultiply (straight -> premultiplied)"),
};

/**
 * Transfer function applied on the way in or out.
 *
 * Unreal render targets usually hold linear values while the viewport shows them
 * encoded. "Match viewport" is the equivalent of Off World Live's "Fix Gamma".
 */
UENUM(BlueprintType)
enum class ESpoutColorSpace : uint8
{
	/** No conversion. Raw values, which is what you want when feeding a compositor working in linear. */
	Raw					UMETA(DisplayName = "Raw (no conversion)"),

	/** Linear -> sRGB. The shared frame then looks like the Unreal viewport. */
	MatchViewport		UMETA(DisplayName = "Match viewport (linear -> sRGB)"),

	/** sRGB -> linear. Use on a receiver when the incoming feed is encoded and your material works in linear. */
	ToLinear			UMETA(DisplayName = "To linear (sRGB -> linear)"),
};

/** Pixel format of the shared texture. */
UENUM(BlueprintType)
enum class ESpoutPixelFormat : uint8
{
	/** Follow the source render target. */
	Auto			UMETA(DisplayName = "Auto (follow source)"),

	/** 8 bit BGRA. The most widely compatible format across Spout applications. */
	BGRA8			UMETA(DisplayName = "8 bit BGRA"),

	/** 8 bit RGBA. */
	RGBA8			UMETA(DisplayName = "8 bit RGBA"),

	/** 10 bit RGB + 2 bit alpha. */
	RGB10A2			UMETA(DisplayName = "10 bit RGB / 2 bit alpha"),

	/** 16 bit float RGBA. HDR capable, supported by most modern Spout receivers. */
	RGBA16F			UMETA(DisplayName = "16 bit float RGBA"),

	/** 32 bit float RGBA. Maximum precision, four times the bandwidth of 8 bit. */
	RGBA32F			UMETA(DisplayName = "32 bit float RGBA"),
};

/** Capture resolution presets, mirroring the usual broadcast ladder. */
UENUM(BlueprintType)
enum class ESpoutResolution : uint8
{
	Custom			UMETA(DisplayName = "Custom"),
	R240p			UMETA(DisplayName = "240p (426 x 240)"),
	R360p			UMETA(DisplayName = "360p (640 x 360)"),
	R480p			UMETA(DisplayName = "480p (854 x 480)"),
	R720p			UMETA(DisplayName = "720p (1280 x 720)"),
	R1080p			UMETA(DisplayName = "1080p (1920 x 1080)"),
	R1440p			UMETA(DisplayName = "1440p (2560 x 1440)"),
	R2160p			UMETA(DisplayName = "4K UHD (3840 x 2160)"),
};

/** Where a sender takes its pixels from. */
UENUM(BlueprintType)
enum class ESpoutSourceMode : uint8
{
	/** Publish the contents of a render target. */
	RenderTarget	UMETA(DisplayName = "Render target"),

	/** Publish the presented frame, HUD and UI included. Needs a game viewport. */
	Viewport		UMETA(DisplayName = "Game viewport"),
};

/** Details of one Spout sender currently published on this machine. */
USTRUCT(BlueprintType)
struct SPOUTUE_API FSpoutSenderInfo
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category = "Spout")
	FString Name;

	UPROPERTY(BlueprintReadOnly, Category = "Spout")
	int32 Width = 0;

	UPROPERTY(BlueprintReadOnly, Category = "Spout")
	int32 Height = 0;

	/** Human readable DXGI format, e.g. "B8G8R8A8_UNORM". */
	UPROPERTY(BlueprintReadOnly, Category = "Spout")
	FString Format;

	/** Frame rate reported by the sender, or 0 when it does not publish one. */
	UPROPERTY(BlueprintReadOnly, Category = "Spout")
	float FrameRate = 0.f;

	/** Frame counter published by the sender, or 0 when frame counting is disabled. */
	UPROPERTY(BlueprintReadOnly, Category = "Spout")
	int64 FrameNumber = 0;

	/** True when this is the machine-wide "active" sender. */
	UPROPERTY(BlueprintReadOnly, Category = "Spout")
	bool bIsActiveSender = false;

	bool IsValid() const { return !Name.IsEmpty() && Width > 0 && Height > 0; }
};

/** Rolling statistics for one sender or receiver, useful for on-screen diagnostics. */
USTRUCT(BlueprintType)
struct SPOUTUE_API FSpoutStreamStats
{
	GENERATED_BODY()

	/** Frames handed to (or pulled from) Spout since the stream was started. */
	UPROPERTY(BlueprintReadOnly, Category = "Spout")
	int64 FrameCount = 0;

	/** Measured rate over the last second. */
	UPROPERTY(BlueprintReadOnly, Category = "Spout")
	float FrameRate = 0.f;

	/**
	 * Ticks on which the sender published no new frame, or the receiver found no
	 * new frame waiting. A steadily climbing value on a receiver means the sender
	 * is slower than Unreal, which is normal, not an error.
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Spout")
	int64 SkippedFrames = 0;

	/** How many times a receiver had to re-latch onto its sender. */
	UPROPERTY(BlueprintReadOnly, Category = "Spout")
	int32 ReconnectCount = 0;

	/** Empty while everything is fine. */
	UPROPERTY(BlueprintReadOnly, Category = "Spout")
	FString LastError;

	void Reset()
	{
		FrameCount = 0;
		FrameRate = 0.f;
		SkippedFrames = 0;
		ReconnectCount = 0;
		LastError.Reset();
	}
};

namespace SpoutUE
{
	/** Resolves a resolution preset to pixels. Returns false for ESpoutResolution::Custom. */
	SPOUTUE_API bool GetPresetResolution(ESpoutResolution Preset, int32& OutWidth, int32& OutHeight);

	/** Maps a plugin pixel format onto an Unreal pixel format. */
	SPOUTUE_API EPixelFormat ToPixelFormat(ESpoutPixelFormat Format, EPixelFormat FallbackForAuto = PF_B8G8R8A8);

	/** True when the format carries more than 8 bits per channel. */
	SPOUTUE_API bool IsHighPrecision(EPixelFormat Format);
}
