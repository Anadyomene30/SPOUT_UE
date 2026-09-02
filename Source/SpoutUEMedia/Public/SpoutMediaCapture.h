// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "MediaCapture.h"
#include "Misc/EngineVersionComparison.h"
#include "SpoutSenderHandle.h"
#include "SpoutMediaCapture.generated.h"

/**
 * The capture half of USpoutMediaOutput.
 *
 * It asks the Media framework for the captured frame as an RHI texture rather
 * than as CPU pixels, which is the whole point: the frame never leaves the GPU
 * on its way to a Spout receiver.
 */
UCLASS(BlueprintType)
class SPOUTUEMEDIA_API USpoutMediaCapture : public UMediaCapture
{
	GENERATED_BODY()

protected:
	//~ UMediaCapture
	virtual bool InitializeCapture() override;
	virtual void StopCaptureImpl(bool bAllowPendingFrameToBeProcess) override;

	/** Spout shares GPU textures, so there is no reason to read anything back. */
	virtual bool ShouldCaptureRHIResource() const override { return true; }

#if UE_VERSION_OLDER_THAN(5, 3, 0)
	virtual void OnRHIResourceCaptured_RenderingThread(
		const FCaptureBaseData& InBaseData,
		TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData,
		FTextureRHIRef InTexture) override;
#else
	virtual void OnRHIResourceCaptured_RenderingThread(
		FRHICommandListImmediate& RHICmdList,
		const FCaptureBaseData& InBaseData,
		TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData,
		FTextureRHIRef InTexture) override;
#endif

private:
	/** Does the actual publish, shared by both signatures above. */
	void PublishFrame_RenderingThread(FRHICommandListImmediate& RHICmdList, FRHITexture* Texture);

	TSharedPtr<FSpoutSenderHandle> Sender;

	/** Snapshotted from the media output when the capture starts. */
	FSpoutSenderSettings Settings;
};
