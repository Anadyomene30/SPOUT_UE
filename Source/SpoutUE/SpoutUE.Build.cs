// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

using UnrealBuildTool;

public class SpoutUE : ModuleRules
{
	public SpoutUE(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		// The D3D12 bridge headers live under Private and are included by path.
		PrivateIncludePaths.Add(System.IO.Path.Combine(ModuleDirectory, "Private"));

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"RHI",
			"RenderCore",
			"CinematicCamera",
			"DeveloperSettings",
			"UMG",
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Projects",
			"Slate",
			"SlateCore",
			"ApplicationCore",
			"SpoutSDK",
		});

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// ID3D12DynamicRHI lives in the D3D12RHI public headers; it is the
			// only supported way to reach Unreal's device, queue and resources.
			PrivateDependencyModuleNames.Add("D3D12RHI");

			PublicSystemLibraries.AddRange(new string[]
			{
				"d3d11.lib",
				"d3d12.lib",
				"dxgi.lib",
				"dxguid.lib",
			});
		}
	}
}
