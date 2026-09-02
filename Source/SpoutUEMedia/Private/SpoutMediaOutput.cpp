// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutMediaOutput.h"

#include "SpoutFunctionLibrary.h"
#include "SpoutMediaCapture.h"

bool USpoutMediaOutput::Validate(FString& OutFailureReason) const
{
	if (!Super::Validate(OutFailureReason))
	{
		return false;
	}

	if (SenderName.IsEmpty())
	{
		OutFailureReason = TEXT("The Spout sender needs a name.");
		return false;
	}

	// Catching this here rather than at capture time means the Media Capture
	// panel says why the output is unusable instead of silently doing nothing.
	const FString UnavailableReason = USpoutFunctionLibrary::GetSpoutUnavailableReason();
	if (!UnavailableReason.IsEmpty())
	{
		OutFailureReason = UnavailableReason;
		return false;
	}

	if (!bUseSourceSize && (OutputSize.X <= 0 || OutputSize.Y <= 0))
	{
		OutFailureReason = TEXT("The output size must be at least one pixel on each side.");
		return false;
	}

	return true;
}

FIntPoint USpoutMediaOutput::GetRequestedSize() const
{
	return bUseSourceSize ? UMediaOutput::RequestCaptureSourceSize : OutputSize;
}

EPixelFormat USpoutMediaOutput::GetRequestedPixelFormat() const
{
	return SpoutUE::ToPixelFormat(PixelFormat, PF_B8G8R8A8);
}

EMediaCaptureConversionOperation USpoutMediaOutput::GetConversionOperation(EMediaCaptureSourceType InSourceType) const
{
	// The plugin's own conversion pass handles the transfer function and the
	// alpha convention, so the Media framework should hand the frame over
	// untouched rather than convert it twice.
	return EMediaCaptureConversionOperation::NONE;
}

UMediaCapture* USpoutMediaOutput::CreateMediaCaptureImpl()
{
	USpoutMediaCapture* Result = NewObject<USpoutMediaCapture>();
	if (Result)
	{
		Result->SetMediaOutput(this);
	}

	return Result;
}
