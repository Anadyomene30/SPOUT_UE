// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutMediaCapture.h"

#include "RHICommandList.h"
#include "SpoutMediaOutput.h"
#include "SpoutUE.h"

bool USpoutMediaCapture::InitializeCapture()
{
	const USpoutMediaOutput* Output = Cast<USpoutMediaOutput>(MediaOutput);
	if (!Output)
	{
		UE_LOG(LogSpout, Error, TEXT("Spout media capture was created without a Spout media output."));
		return false;
	}

	Settings.SenderName = Output->SenderName;
	Settings.PixelFormat = Output->PixelFormat;
	Settings.ColorSpace = Output->ColorSpace;
	Settings.AlphaMode = Output->AlphaMode;
	Settings.bFlipVertical = Output->bFlipVertical;

	Sender = FSpoutSenderHandle::Create();
	if (!Sender.IsValid())
	{
		UE_LOG(LogSpout, Error, TEXT("Spout media capture cannot start: Spout needs the D3D12 RHI on Windows."));
		return false;
	}

	UE_LOG(LogSpout, Log, TEXT("Spout media capture starting as '%s'."), *Settings.SenderName);
	return true;
}

void USpoutMediaCapture::StopCaptureImpl(bool bAllowPendingFrameToBeProcess)
{
	if (Sender.IsValid())
	{
		Sender->Destroy();
		Sender.Reset();
	}
}

void USpoutMediaCapture::PublishFrame_RenderingThread(FRHICommandListImmediate& RHICmdList, FRHITexture* Texture)
{
	if (!Sender.IsValid() || !Texture)
	{
		return;
	}

	Sender->Send_RenderThread(RHICmdList, Texture, Settings);
}

#if UE_VERSION_OLDER_THAN(5, 3, 0)
void USpoutMediaCapture::OnRHIResourceCaptured_RenderingThread(
	const FCaptureBaseData& InBaseData,
	TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData,
	FTextureRHIRef InTexture)
{
	PublishFrame_RenderingThread(FRHICommandListExecutor::GetImmediateCommandList(), InTexture.GetReference());
}
#else
void USpoutMediaCapture::OnRHIResourceCaptured_RenderingThread(
	FRHICommandListImmediate& RHICmdList,
	const FCaptureBaseData& InBaseData,
	TSharedPtr<FMediaCaptureUserData, ESPMode::ThreadSafe> InUserData,
	FTextureRHIRef InTexture)
{
	PublishFrame_RenderingThread(RHICmdList, InTexture.GetReference());
}
#endif
