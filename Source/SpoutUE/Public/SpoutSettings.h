// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "SpoutUETypes.h"
#include "SpoutSettings.generated.h"

/**
 * Project-wide Spout defaults, under Project Settings > Plugins > Spout.
 */
UCLASS(Config = Engine, DefaultConfig, meta = (DisplayName = "Spout"))
class SPOUTUE_API USpoutSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	USpoutSettings();

	static const USpoutSettings* Get();

	//~ UDeveloperSettings
	virtual FName GetCategoryName() const override;

	/**
	 * Name new senders start from when none is set on the component.
	 *
	 * Spout disambiguates duplicates itself by appending _1, _2 and so on, so
	 * several Unreal instances can run side by side without colliding.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Senders")
	FString DefaultSenderName = TEXT("UnrealEngine");

	/** Default transfer function for new senders. Match viewport is the usual choice for live output. */
	UPROPERTY(Config, EditAnywhere, Category = "Senders")
	ESpoutColorSpace DefaultSenderColorSpace = ESpoutColorSpace::MatchViewport;

	/** Default alpha convention for new senders. */
	UPROPERTY(Config, EditAnywhere, Category = "Senders")
	ESpoutAlphaMode DefaultSenderAlphaMode = ESpoutAlphaMode::Opaque;

	/** Default transfer function for new receivers. */
	UPROPERTY(Config, EditAnywhere, Category = "Receivers")
	ESpoutColorSpace DefaultReceiverColorSpace = ESpoutColorSpace::ToLinear;

	/**
	 * How many senders Spout's shared memory map can hold, machine-wide.
	 *
	 * Raising it reserves more shared memory for every Spout application on the
	 * system, so only raise it if you genuinely run more than the default.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Advanced", meta = (ClampMin = "1", ClampMax = "255"))
	int32 MaxSenders = 10;

	/**
	 * Seconds between refreshes of the machine-wide sender list.
	 *
	 * The list is read from shared memory, so this is cheap; it is throttled only
	 * so that the editor pickers do not rebuild on every frame.
	 */
	UPROPERTY(Config, EditAnywhere, Category = "Advanced", meta = (ClampMin = "0.05", ClampMax = "5.0"))
	float SenderDiscoveryInterval = 0.5f;

	/** Mirrors Spout's own log output into LogSpout. Noisy, but the first thing to turn on when a feed will not connect. */
	UPROPERTY(Config, EditAnywhere, Category = "Advanced")
	bool bVerboseSpoutLogging = false;
};
