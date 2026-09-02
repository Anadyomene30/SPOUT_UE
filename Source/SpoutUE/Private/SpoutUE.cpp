// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutUE.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "RHI.h"
#include "ShaderCore.h"

#if PLATFORM_WINDOWS
#include "D3D12/SpoutD3D12Device.h"
#endif

#define LOCTEXT_NAMESPACE "FSpoutUEModule"

DEFINE_LOG_CATEGORY(LogSpout);

void FSpoutUEModule::StartupModule()
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SpoutUE"));
	if (Plugin.IsValid())
	{
		const FString ShaderDirectory = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
		AddShaderSourceDirectoryMapping(TEXT("/Plugin/SpoutUE"), ShaderDirectory);
	}
	else
	{
		UE_LOG(LogSpout, Error, TEXT("Could not locate the SpoutUE plugin directory; conversion shaders will not compile."));
	}

	// The bridge itself is created lazily: at module startup the RHI may not be
	// initialised yet, and a project that never touches Spout should not pay for
	// a D3D11 runtime it will not use.
}

void FSpoutUEModule::ShutdownModule()
{
	FScopeLock Lock(&DeviceCS);
	Device.Reset();
}

bool FSpoutUEModule::IsRHISupported()
{
#if PLATFORM_WINDOWS
	return GDynamicRHI != nullptr && RHIGetInterfaceType() == ERHIInterfaceType::D3D12;
#else
	return false;
#endif
}

FSpoutD3D12Device* FSpoutUEModule::GetDevice()
{
#if PLATFORM_WINDOWS
	FScopeLock Lock(&DeviceCS);

	if (!Device.IsValid())
	{
		static bool bWarnedOnce = false;

		Device = FSpoutD3D12Device::Create();

		if (!Device.IsValid() && !bWarnedOnce)
		{
			bWarnedOnce = true;
			UE_LOG(LogSpout, Error, TEXT("Spout sharing is unavailable: the D3D11On12 bridge could not be created."));
		}
	}

	return Device.Get();
#else
	return nullptr;
#endif
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSpoutUEModule, SpoutUE)
