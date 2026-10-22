// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

using UnrealBuildTool;

public class SpoutUEShaders : ModuleRules
{
	public SpoutUEShaders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"RHI",
			"RenderCore",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",
		});
	}
}
