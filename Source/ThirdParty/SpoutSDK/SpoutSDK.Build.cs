// Copyright (c) 2026 SpoutUE contributors. MIT licensed.
// Wraps the vendored Spout 2 SDK (BSD 2-Clause, (c) 2020-2024 Lynn Jarvis) as an
// Unreal module so that the rest of the plugin can link against it without any
// external DLL to stage.

using System.IO;
using UnrealBuildTool;

public class SpoutSDK : ModuleRules
{
	public SpoutSDK(ReadOnlyTargetRules Target) : base(Target)
	{
		// Third-party C++ that knows nothing about Unreal: keep it out of unity
		// builds and out of the shared engine PCH, but force-include our own tiny
		// PCH so we can silence the warnings this code legitimately trips.
		bUseUnity = false;
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PrivatePCHHeaderFile = "Private/SpoutSDKPrivatePCH.h";

		// SpoutDirectX / SpoutFrameCount / SpoutSharedMemory use try/catch.
		bEnableExceptions = true;

		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Spout"));

		PublicDependencyModuleNames.Add("Core");

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			// Spout itself declares most of these with #pragma comment(lib, ...),
			// but being explicit keeps the link deterministic.
			PublicSystemLibraries.AddRange(new string[]
			{
				"d3d11.lib",
				"d3d12.lib",
				"dxgi.lib",
				"dxguid.lib",
				"psapi.lib",
				"shell32.lib",
				"comctl32.lib",
				"advapi32.lib",
				"user32.lib",
				"gdi32.lib",
				"ole32.lib",
				"shlwapi.lib",
			});

			// In a modular (editor) build the SDK lives in its own DLL, so its
			// symbols have to be exported/imported. In a monolithic build
			// everything is statically linked and the macros must stay empty,
			// otherwise MSVC looks for __imp_ thunks that do not exist.
			if (Target.LinkType != TargetLinkType.Monolithic)
			{
				PublicDefinitions.Add("SPOUT_IMPORT_DLL=1");
				PrivateDefinitions.Add("SPOUT_BUILD_DLL=1");
			}
		}

		PrivateDefinitions.Add("_CRT_SECURE_NO_WARNINGS=1");
		PrivateDefinitions.Add("_CRT_NONSTDC_NO_DEPRECATE=1");
	}
}
