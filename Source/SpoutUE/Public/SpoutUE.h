// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

SPOUTUE_API DECLARE_LOG_CATEGORY_EXTERN(LogSpout, Log, All);

class FSpoutD3D12Device;

/**
 * Runtime module for the Spout plugin.
 *
 * Owns the lifetime of the shared D3D11On12 bridge, which is reached through GetSpoutDevice below.
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

	/** True when the running RHI can host Spout sharing (Windows + D3D12). */
	static bool IsRHISupported();

	/**
	 * Senders and receivers running in an editor world, as opposed to PIE or a game.
	 *
	 * The editor only ticks its world while a viewport is realtime and throttles
	 * itself when it is not the foreground application, and either one freezes
	 * editor-world output while the user is working in Resolume or OBS. The editor
	 * module keeps the editor awake for as long as this is non-zero. Game thread only.
	 */
	static void NotifyEditorStreamStarted();
	static void NotifyEditorStreamStopped();
	static int32 GetEditorStreamCount();

	/**
	 * Set by the editor module while PIE runs. Editor-world senders stand down so
	 * that the PIE copies of the same actors publish under the original names,
	 * and receivers following those names switch to the game without a change on
	 * their side. Game thread only.
	 */
	static void SetEditorSendersPaused(bool bPaused);
	static bool AreEditorSendersPaused();
};

/**
 * The shared D3D11On12 bridge, created lazily on first use from Unreal's own
 * D3D12 device and graphics queue.
 *
 * A free function rather than a module method on purpose: the streams reach for
 * it on the render thread, and FModuleManager is not something to touch from
 * there. Returns nullptr when the running RHI is not D3D12, which is the
 * plugin's only supported configuration.
 */
SPOUTUE_API FSpoutD3D12Device* GetSpoutDevice();
