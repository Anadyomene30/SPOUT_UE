// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

SPOUTUE_API DECLARE_LOG_CATEGORY_EXTERN(LogSpout, Log, All);

class FSpoutD3D12Device;

/**
 * Runtime module for the Spout plugin.
 *
 * Owns the process-wide D3D11On12 bridge that every sender and receiver shares,
 * and registers the plugin's shader source directory.
 */
class SPOUTUE_API FSpoutUEModule : public IModuleInterface
{
public:
	//~ IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static FSpoutUEModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FSpoutUEModule>(TEXT("SpoutUE"));
	}

	static bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded(TEXT("SpoutUE"));
	}

	/**
	 * The shared D3D11On12 bridge, created lazily on first use from Unreal's own
	 * D3D12 device and graphics queue.
	 *
	 * Returns nullptr when the running RHI is not D3D12, which is the plugin's
	 * only supported configuration.
	 */
	FSpoutD3D12Device* GetDevice();

	/** True when the running RHI can host Spout sharing (Windows + D3D12). */
	static bool IsRHISupported();

private:
	TUniquePtr<FSpoutD3D12Device> Device;
	FCriticalSection DeviceCS;
};
