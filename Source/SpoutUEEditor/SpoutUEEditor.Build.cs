// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

using UnrealBuildTool;

public class SpoutUEEditor : ModuleRules
{
	public SpoutUEEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"SpoutUE",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"InputCore",
			"PropertyEditor",
			"Projects",
			"Slate",
			"SlateCore",
			"ToolWidgets",
			"UnrealEd",
		});
	}
}
