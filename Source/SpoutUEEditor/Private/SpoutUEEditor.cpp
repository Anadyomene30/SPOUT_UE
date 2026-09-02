// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutUEEditor.h"

#include "IPlacementModeModule.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "SpoutCineCameraActor.h"
#include "SpoutManagerActors.h"
#include "SpoutReceiverComponent.h"
#include "SpoutSenderComponent.h"
#include "SpoutSenderPickerCustomization.h"
#include "Styling/SlateIconFinder.h"

#define LOCTEXT_NAMESPACE "FSpoutUEEditorModule"

namespace
{
	const FName SpoutPlacementCategoryHandle = TEXT("Spout");
}

void FSpoutUEEditorModule::StartupModule()
{
	RegisterDetailCustomizations();
	RegisterPlacementCategory();
}

void FSpoutUEEditorModule::ShutdownModule()
{
	UnregisterPlacementCategory();
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

void FSpoutUEEditorModule::RegisterPlacementCategory()
{
	if (!IPlacementModeModule::IsAvailable())
	{
		return;
	}

	IPlacementModeModule& PlacementModule = IPlacementModeModule::Get();

	// Sort order 45 puts Spout just after the engine's own categories rather than
	// at the very bottom of a long list.
	const FPlacementCategoryInfo CategoryInfo(
		LOCTEXT("SpoutPlacementCategory", "Spout"),
		FSlateIconFinder::FindIconForClass(ASpoutSenderActor::StaticClass()),
		SpoutPlacementCategoryHandle,
		TEXT("PMSpout"),
		45);

	if (!PlacementModule.RegisterPlacementCategory(CategoryInfo))
	{
		return;
	}

	bPlacementCategoryRegistered = true;

	PlacementModule.RegisterPlaceableItem(SpoutPlacementCategoryHandle,
		MakeShared<FPlaceableItem>(*ASpoutSenderActor::StaticClass()));
	PlacementModule.RegisterPlaceableItem(SpoutPlacementCategoryHandle,
		MakeShared<FPlaceableItem>(*ASpoutReceiverActor::StaticClass()));
	PlacementModule.RegisterPlaceableItem(SpoutPlacementCategoryHandle,
		MakeShared<FPlaceableItem>(*ASpoutCineCameraActor::StaticClass()));
}

void FSpoutUEEditorModule::UnregisterPlacementCategory()
{
	if (!bPlacementCategoryRegistered || !IPlacementModeModule::IsAvailable())
	{
		return;
	}

	IPlacementModeModule::Get().UnregisterPlacementCategory(SpoutPlacementCategoryHandle);
	bPlacementCategoryRegistered = false;
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSpoutUEEditorModule, SpoutUEEditor)
