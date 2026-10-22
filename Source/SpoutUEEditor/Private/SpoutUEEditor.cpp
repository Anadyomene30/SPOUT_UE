// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutUEEditor.h"

#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "SpoutReceiverComponent.h"
#include "SpoutSenderComponent.h"
#include "SpoutSenderPickerCustomization.h"

#define LOCTEXT_NAMESPACE "FSpoutUEEditorModule"

//
// There is deliberately no Place Actors category registration here.
//
// FPlaceableItem's constructors are the kind of editor API that shifts between
// engine releases, and a compile error in this module takes the whole editor
// build down with it. The three actors are placeable UCLASSes, so the Place
// Actors panel already lists them under All Classes and finds them by typing
// "Spout" — a category would be presentation, bought at the price of a build
// risk on every engine upgrade.
//

void FSpoutUEEditorModule::StartupModule()
{
	RegisterDetailCustomizations();
}

void FSpoutUEEditorModule::ShutdownModule()
{
	UnregisterDetailCustomizations();
}

void FSpoutUEEditorModule::RegisterDetailCustomizations()
{
	FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	PropertyModule.RegisterCustomClassLayout(
		USpoutReceiverComponent::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FSpoutReceiverComponentDetails::MakeInstance));
	CustomizedClasses.Add(USpoutReceiverComponent::StaticClass()->GetFName());

	PropertyModule.RegisterCustomClassLayout(
		USpoutSenderComponent::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FSpoutSenderComponentDetails::MakeInstance));
	CustomizedClasses.Add(USpoutSenderComponent::StaticClass()->GetFName());

	PropertyModule.NotifyCustomizationModuleChanged();
}

void FSpoutUEEditorModule::UnregisterDetailCustomizations()
{
	// On editor shutdown the property module may already be gone, and asking for
	// it would load it again just to unregister from it.
	if (!FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
	{
		CustomizedClasses.Reset();
		return;
	}

	FPropertyEditorModule& PropertyModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	for (const FName& ClassName : CustomizedClasses)
	{
		PropertyModule.UnregisterCustomClassLayout(ClassName);
	}

	CustomizedClasses.Reset();
	PropertyModule.NotifyCustomizationModuleChanged();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSpoutUEEditorModule, SpoutUEEditor)
