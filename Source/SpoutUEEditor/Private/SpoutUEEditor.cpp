// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutUEEditor.h"

#include "Editor.h"
#include "Editor/EditorPerformanceSettings.h"
#include "LevelEditorViewport.h"
#include "Misc/CoreDelegates.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "SpoutReceiverComponent.h"
#include "SpoutSenderComponent.h"
#include "SpoutSenderPickerCustomization.h"
#include "SpoutUE.h"

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

namespace
{
	FText GetRealtimeOverrideName()
	{
		return LOCTEXT("RealtimeOverride", "Spout output");
	}
}

void FSpoutUEEditorModule::StartupModule()
{
	RegisterDetailCustomizations();

	// The module loads at PostEngineInit, so GEditor exists by now.
	RegisterKeepAwake();

	// Undone while GEditor and the settings objects still exist; by the time
	// ShutdownModule runs on exit, the UObject system is already gone.
	FCoreDelegates::OnEnginePreExit.AddRaw(this, &FSpoutUEEditorModule::UnregisterKeepAwake);
}

void FSpoutUEEditorModule::ShutdownModule()
{
	FCoreDelegates::OnEnginePreExit.RemoveAll(this);

	// Only reached with live UObjects when the module is unloaded on its own.
	if (!IsEngineExitRequested())
	{
		UnregisterKeepAwake();
	}

	UnregisterDetailCustomizations();
}

void FSpoutUEEditorModule::RegisterKeepAwake()
{
	if (!GEditor || KeepAwakeTickerHandle.IsValid())
	{
		return;
	}

	// A minimised editor caps itself at 3 fps whatever the background setting
	// says; this is the hook Epic provides to opt out of that.
	UEditorEngine::FShouldDisableCPUThrottling Delegate = UEditorEngine::FShouldDisableCPUThrottling::CreateLambda([]()
	{
		return FSpoutUEModule::GetEditorStreamCount() > 0;
	});
	ThrottleDelegateHandle = Delegate.GetHandle();
	GEditor->ShouldDisableCPUThrottlingDelegates.Add(MoveTemp(Delegate));

	// Viewports come and go with layouts, so the state is reconciled on a timer
	// rather than set once.
	KeepAwakeTickerHandle = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateRaw(this, &FSpoutUEEditorModule::UpdateKeepAwake), 0.25f);
}

void FSpoutUEEditorModule::UnregisterKeepAwake()
{
	if (KeepAwakeTickerHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(KeepAwakeTickerHandle);
		KeepAwakeTickerHandle.Reset();
	}

	SetRealtimeOverride(false);
	SetBackgroundThrottleSuspended(false);

	if (GEditor && ThrottleDelegateHandle.IsValid())
	{
		const FDelegateHandle Handle = ThrottleDelegateHandle;
		GEditor->ShouldDisableCPUThrottlingDelegates.RemoveAll([Handle](const UEditorEngine::FShouldDisableCPUThrottling& Delegate)
		{
			return Delegate.GetHandle() == Handle;
		});
	}
	ThrottleDelegateHandle.Reset();
}

bool FSpoutUEEditorModule::UpdateKeepAwake(float DeltaTime)
{
	const bool bStreamsInEditor = FSpoutUEModule::GetEditorStreamCount() > 0;
	SetRealtimeOverride(bStreamsInEditor);
	SetBackgroundThrottleSuspended(bStreamsInEditor);
	return true;
}

void FSpoutUEEditorModule::SetBackgroundThrottleSuspended(bool bSuspended)
{
	// With "Use Less CPU when in Background" on, every editor tick without focus
	// pushes a non-realtime override on top of every viewport, ours included, and
	// the editor world stops ticking. There is no hook around it, so the setting
	// itself is lifted, in memory only, for as long as Spout runs in the editor.
	UEditorPerformanceSettings* Settings = GetMutableDefault<UEditorPerformanceSettings>();
	if (!Settings || bSuspended == bThrottleSuspended)
	{
		return;
	}

	if (bSuspended)
	{
		bThrottleWasEnabled = Settings->bThrottleCPUWhenNotForeground;
		if (bThrottleWasEnabled)
		{
			Settings->bThrottleCPUWhenNotForeground = false;
			UE_LOG(LogSpout, Log, TEXT("Spout is running in the editor: 'Use Less CPU when in Background' is suspended until it stops, so output keeps flowing while another application has focus."));
		}
	}
	else if (bThrottleWasEnabled && !Settings->bThrottleCPUWhenNotForeground)
	{
		// Restored only if nobody changed it in the meantime.
		Settings->bThrottleCPUWhenNotForeground = true;
	}

	bThrottleSuspended = bSuspended;
}

void FSpoutUEEditorModule::SetRealtimeOverride(bool bEnabled)
{
	if (!GEditor)
	{
		return;
	}

	const FText OverrideName = GetRealtimeOverrideName();

	// Perspective viewports only: one realtime viewport is enough to keep the
	// editor world ticking, and the orthographic ones are rarely on screen.
	for (FLevelEditorViewportClient* ViewportClient : GEditor->GetLevelViewportClients())
	{
		if (!ViewportClient || !ViewportClient->IsPerspective())
		{
			continue;
		}

		const bool bHasOverride = ViewportClient->HasRealtimeOverride(OverrideName);
		if (bEnabled && !bHasOverride)
		{
			ViewportClient->AddRealtimeOverride(true, OverrideName);
		}
		else if (!bEnabled && bHasOverride)
		{
			ViewportClient->RemoveRealtimeOverride(OverrideName);
		}
	}
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
