// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Modules/ModuleInterface.h"

/**
 * Editor-only conveniences: a picker instead of a bare sender-name text field,
 * live status on sender components, and keeping the editor awake while Spout
 * streams run in the editor world.
 */
class FSpoutUEEditorModule : public IModuleInterface
{
public:
	//~ IModuleInterface
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterDetailCustomizations();
	void UnregisterDetailCustomizations();

	/**
	 * While editor-world streams run, holds the level viewports realtime and stops
	 * the editor going idle in the background, without which the editor world does
	 * not tick and every sender freezes on its last frame.
	 */
	void RegisterKeepAwake();
	void UnregisterKeepAwake();
	bool UpdateKeepAwake(float DeltaTime);
	void SetRealtimeOverride(bool bEnabled);
	void SetBackgroundThrottleSuspended(bool bSuspended);

	/** Editor-world senders stand down for the length of a PIE session. */
	void OnPreBeginPIE(bool bIsSimulating);
	void OnEndPIE(bool bIsSimulating);

	TArray<FName> CustomizedClasses;

	TArray<TWeakObjectPtr<class USpoutSenderComponent>> PausedSenders;

	FTSTicker::FDelegateHandle KeepAwakeTickerHandle;
	FDelegateHandle ThrottleDelegateHandle;
	bool bThrottleSuspended = false;
	bool bThrottleWasEnabled = false;
};
