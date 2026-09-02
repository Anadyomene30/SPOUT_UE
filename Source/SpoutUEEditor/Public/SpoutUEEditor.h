// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleInterface.h"

/**
 * Editor-only conveniences: a sender picker instead of a bare text field, live
 * status on sender components, and a Spout category in the Place Actors panel.
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

	void RegisterPlacementCategory();
	void UnregisterPlacementCategory();

	TArray<FName> CustomizedClasses;
	bool bPlacementCategoryRegistered = false;
};
