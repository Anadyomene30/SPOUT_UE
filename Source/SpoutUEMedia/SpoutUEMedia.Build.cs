// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

using UnrealBuildTool;

public class SpoutUEMedia : ModuleRules
{
	public SpoutUEMedia(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"MediaIOCore",
			"SpoutUE",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"RHI",
			"RenderCore",
		});

		// The bridge headers are private to SpoutUE, so the capture reaches Spout
		// through the same public stream API the components use.
		PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "Private"));
	}
}
