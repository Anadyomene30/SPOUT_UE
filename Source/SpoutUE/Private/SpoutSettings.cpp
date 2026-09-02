// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutSettings.h"

USpoutSettings::USpoutSettings()
{
	CategoryName = TEXT("Plugins");
	SectionName = TEXT("Spout");
}

const USpoutSettings* USpoutSettings::Get()
{
	return GetDefault<USpoutSettings>();
}

FName USpoutSettings::GetCategoryName() const
{
	return TEXT("Plugins");
}
