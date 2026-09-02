// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "MediaOutput.h"
#include "SpoutUETypes.h"
#include "SpoutMediaOutput.generated.h"

class UMediaCapture;

/**
 * A Spout sender exposed as a Media Output asset.
 *
 * This is the route into everything in Unreal that already speaks the Media
 * framework: nDisplay output mapping, Movie Render Queue, the Media Capture
 * panel, and any Blueprint that drives a capture. The sender components are the
 * simpler option for a single feed; this one is what you want when the output
 * has to be selected from a list of media outputs alongside NDI or SDI.
 */
UCLASS(BlueprintType, meta = (DisplayName = "Spout Media Output"))
class SPOUTUEMEDIA_API USpoutMediaOutput : public UMediaOutput
{
	GENERATED_BODY()

public:
	/** Name the sender is published under. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout")
	FString SenderName = TEXT("UnrealMediaOutput");

	/**
	 * Output size.
	 *
	 * Leave "Use source size" on to follow whatever is being captured, which is
	 * almost always what you want; a fixed size makes the capture rescale.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout")
	bool bUseSourceSize = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout", meta = (EditCondition = "!bUseSourceSize", ClampMin = "1", ClampMax = "16384"))
	FIntPoint OutputSize = FIntPoint(1920, 1080);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout")
	ESpoutPixelFormat PixelFormat = ESpoutPixelFormat::BGRA8;

	/** Transfer function applied on the way out. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout")
	ESpoutColorSpace ColorSpace = ESpoutColorSpace::MatchViewport;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout")
	ESpoutAlphaMode AlphaMode = ESpoutAlphaMode::Opaque;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout")
	bool bFlipVertical = false;

	//~ UMediaOutput
	virtual bool Validate(FString& OutFailureReason) const override;
	virtual FIntPoint GetRequestedSize() const override;
	virtual EPixelFormat GetRequestedPixelFormat() const override;
	virtual EMediaCaptureConversionOperation GetConversionOperation(EMediaCaptureSourceType InSourceType) const override;

protected:
	virtual UMediaCapture* CreateMediaCaptureImpl() override;
};
