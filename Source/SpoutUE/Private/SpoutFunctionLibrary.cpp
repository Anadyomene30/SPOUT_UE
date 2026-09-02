// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutFunctionLibrary.h"

#include "Engine/Engine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "RHI.h"
#include "SpoutSubsystem.h"
#include "SpoutUE.h"

#if PLATFORM_WINDOWS
#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include "SpoutUtils.h"
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"
#endif

bool USpoutFunctionLibrary::IsSpoutSupported()
{
	return FSpoutUEModule::IsRHISupported();
}

FString USpoutFunctionLibrary::GetSpoutUnavailableReason()
{
#if !PLATFORM_WINDOWS
	return TEXT("Spout is a Windows-only technology.");
#else
	if (!GDynamicRHI)
	{
		return TEXT("The RHI has not been initialised yet.");
	}

	if (RHIGetInterfaceType() != ERHIInterfaceType::D3D12)
	{
		return FString::Printf(
			TEXT("Spout needs the D3D12 RHI, but this session is running %s. Set Default RHI to DirectX 12 in Project Settings > Platforms > Windows, or launch with -d3d12."),
			GDynamicRHI->GetName());
	}

	return FString();
#endif
}

TArray<FSpoutSenderInfo> USpoutFunctionLibrary::GetSpoutSenders()
{
	if (USpoutSubsystem* Subsystem = USpoutSubsystem::Get())
	{
		return Subsystem->GetSenders();
	}

	return TArray<FSpoutSenderInfo>();
}

TArray<FString> USpoutFunctionLibrary::GetSpoutSenderNames()
{
	if (USpoutSubsystem* Subsystem = USpoutSubsystem::Get())
	{
		return Subsystem->GetSenderNames();
	}

	return TArray<FString>();
}

bool USpoutFunctionLibrary::GetSpoutSenderInfo(const FString& SenderName, FSpoutSenderInfo& OutInfo)
{
	if (USpoutSubsystem* Subsystem = USpoutSubsystem::Get())
	{
		return Subsystem->GetSenderInfo(SenderName, OutInfo);
	}

	OutInfo = FSpoutSenderInfo();
	return false;
}

FString USpoutFunctionLibrary::GetActiveSpoutSender()
{
	if (USpoutSubsystem* Subsystem = USpoutSubsystem::Get())
	{
		return Subsystem->GetActiveSenderName();
	}

	return FString();
}

bool USpoutFunctionLibrary::SetActiveSpoutSender(const FString& SenderName)
{
	if (USpoutSubsystem* Subsystem = USpoutSubsystem::Get())
	{
		return Subsystem->SetActiveSenderName(SenderName);
	}

	return false;
}

FString USpoutFunctionLibrary::GetSpoutSDKVersion()
{
#if PLATFORM_WINDOWS
	return UTF8_TO_TCHAR(spoututils::GetSDKversion().c_str());
#else
	return FString();
#endif
}

UTextureRenderTarget2D* USpoutFunctionLibrary::CreateSpoutRenderTarget(
	UObject* WorldContextObject,
	int32 Width,
	int32 Height,
	ESpoutPixelFormat PixelFormat,
	FLinearColor ClearColor)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull) : nullptr;
	if (!World)
	{
		return nullptr;
	}

	Width = FMath::Clamp(Width, 1, 16384);
	Height = FMath::Clamp(Height, 1, 16384);

	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>(WorldContextObject);
	RenderTarget->ClearColor = ClearColor;

	switch (PixelFormat)
	{
	case ESpoutPixelFormat::RGB10A2:	RenderTarget->RenderTargetFormat = RTF_RGB10A2; break;
	case ESpoutPixelFormat::RGBA16F:	RenderTarget->RenderTargetFormat = RTF_RGBA16f; break;
	case ESpoutPixelFormat::RGBA32F:	RenderTarget->RenderTargetFormat = RTF_RGBA32f; break;
	case ESpoutPixelFormat::BGRA8:
	case ESpoutPixelFormat::RGBA8:
	case ESpoutPixelFormat::Auto:
	default:							RenderTarget->RenderTargetFormat = RTF_RGBA8; break;
	}

	// 8 bit targets are what an external compositor expects to receive already
	// encoded; higher precision formats stay linear so nothing is clipped.
	const EPixelFormat ResolvedFormat = SpoutUE::ToPixelFormat(PixelFormat, PF_B8G8R8A8);
	RenderTarget->bForceLinearGamma = SpoutUE::IsHighPrecision(ResolvedFormat);

	RenderTarget->InitAutoFormat(Width, Height);
	RenderTarget->UpdateResourceImmediate(true);

	return RenderTarget;
}

FIntPoint USpoutFunctionLibrary::GetSpoutResolution(ESpoutResolution Resolution, FIntPoint CustomResolution)
{
	int32 Width = 0;
	int32 Height = 0;

	if (SpoutUE::GetPresetResolution(Resolution, Width, Height))
	{
		return FIntPoint(Width, Height);
	}

	return CustomResolution;
}
