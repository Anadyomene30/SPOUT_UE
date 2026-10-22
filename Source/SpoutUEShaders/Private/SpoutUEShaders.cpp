// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutUEShaders.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "ShaderCore.h"

DEFINE_LOG_CATEGORY_STATIC(LogSpoutShaders, Log, All);

IMPLEMENT_GLOBAL_SHADER(FSpoutConversionPS, "/Plugin/SpoutUE/Private/SpoutConversion.usf", "MainPS", SF_Pixel);
IMPLEMENT_GLOBAL_SHADER(FSpoutOverlayPS, "/Plugin/SpoutUE/Private/SpoutConversion.usf", "ComposeOverlayPS", SF_Pixel);

class FSpoutUEShadersModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("SpoutUE"));
		if (Plugin.IsValid())
		{
			const FString ShaderDirectory = FPaths::Combine(Plugin->GetBaseDir(), TEXT("Shaders"));
			AddShaderSourceDirectoryMapping(TEXT("/Plugin/SpoutUE"), ShaderDirectory);
		}
		else
		{
			UE_LOG(LogSpoutShaders, Error, TEXT("Could not locate the SpoutUE plugin directory; conversion shaders will not compile."));
		}
	}
};

IMPLEMENT_MODULE(FSpoutUEShadersModule, SpoutUEShaders)
