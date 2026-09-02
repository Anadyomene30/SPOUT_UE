// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "SpoutUETypes.h"
#include "SpoutFunctionLibrary.generated.h"

class UObject;
class UTextureRenderTarget2D;

/** Blueprint entry points for discovery and for the odd job that has no component. */
UCLASS()
class SPOUTUE_API USpoutFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** True when this machine and RHI can share Spout textures (Windows + D3D12). */
	UFUNCTION(BlueprintPure, Category = "Spout", meta = (DisplayName = "Is Spout Supported"))
	static bool IsSpoutSupported();

	/**
	 * Why Spout is unavailable, or an empty string when it is available.
	 *
	 * Worth surfacing in a tool UI: the answer is almost always "the project is
	 * running on the wrong RHI", which is invisible otherwise.
	 */
	UFUNCTION(BlueprintPure, Category = "Spout")
	static FString GetSpoutUnavailableReason();

	/** Every sender published on this machine, Unreal's own included. */
	UFUNCTION(BlueprintCallable, Category = "Spout")
	static TArray<FSpoutSenderInfo> GetSpoutSenders();

	UFUNCTION(BlueprintCallable, Category = "Spout")
	static TArray<FString> GetSpoutSenderNames();

	UFUNCTION(BlueprintCallable, Category = "Spout")
	static bool GetSpoutSenderInfo(const FString& SenderName, FSpoutSenderInfo& OutInfo);

	/** The sender receivers fall back to when they are given no name. */
	UFUNCTION(BlueprintCallable, Category = "Spout")
	static FString GetActiveSpoutSender();

	UFUNCTION(BlueprintCallable, Category = "Spout")
	static bool SetActiveSpoutSender(const FString& SenderName);

	/** Version string of the embedded Spout SDK. */
	UFUNCTION(BlueprintPure, Category = "Spout")
	static FString GetSpoutSDKVersion();

	/**
	 * Creates a transient render target sized and formatted for Spout work.
	 *
	 * A convenience over the generic node: it forces linear gamma off for 8 bit
	 * targets, which is what keeps a round trip through another application from
	 * shifting the midtones.
	 */
	UFUNCTION(BlueprintCallable, Category = "Spout", meta = (WorldContext = "WorldContextObject"))
	static UTextureRenderTarget2D* CreateSpoutRenderTarget(
		UObject* WorldContextObject,
		int32 Width = 1920,
		int32 Height = 1080,
		ESpoutPixelFormat PixelFormat = ESpoutPixelFormat::BGRA8,
		FLinearColor ClearColor = FLinearColor::Black);

	/** Pixel size of a resolution preset. */
	UFUNCTION(BlueprintPure, Category = "Spout")
	static FIntPoint GetSpoutResolution(ESpoutResolution Resolution, FIntPoint CustomResolution);
};
