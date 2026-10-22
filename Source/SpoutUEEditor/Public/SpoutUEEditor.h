// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

/**
 * Editor-only conveniences: a picker instead of a bare sender-name text field,
 * and live status on sender components.
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

	TArray<FName> CustomizedClasses;
};
