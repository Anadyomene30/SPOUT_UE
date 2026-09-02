// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "Modules/ModuleInterface.h"

/**
 * Thin Unreal module wrapper around the vendored Spout 2 SDK.
 *
 * The module carries no logic of its own: it exists so that the Spout sources
 * are compiled and linked by UnrealBuildTool with the right flags, and so the
 * plugin ships without any loose DLL to stage into packaged builds.
 */
class FSpoutSDKModule : public IModuleInterface
{
};
