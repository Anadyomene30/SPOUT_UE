// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "Subsystems/EngineSubsystem.h"
#include "SpoutUETypes.h"
#include "SpoutSubsystem.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSpoutSenderEvent, const FSpoutSenderInfo&, SenderInfo);
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FSpoutSenderListEvent);

/**
 * Machine-wide view of the Spout senders currently published.
 *
 * Spout keeps its sender directory in shared memory, so this is a window onto
 * every Spout application running on the box, not just Unreal's own senders.
 * The list is polled rather than pushed because Spout has no notification
 * channel; the interval is configurable in project settings.
 *
 * This is an engine subsystem so the editor can populate sender pickers without
 * a running world.
 */
UCLASS(DisplayName = "Spout Subsystem")
class SPOUTUE_API USpoutSubsystem : public UEngineSubsystem
{
	GENERATED_BODY()

public:
	//~ USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static USpoutSubsystem* Get();

	/** Senders seen at the last refresh. */
	UFUNCTION(BlueprintCallable, Category = "Spout")
	TArray<FSpoutSenderInfo> GetSenders() const { return Senders; }

	UFUNCTION(BlueprintCallable, Category = "Spout")
	TArray<FString> GetSenderNames() const;

	/** Reads one sender's details straight from shared memory, bypassing the cache. */
	UFUNCTION(BlueprintCallable, Category = "Spout")
	bool GetSenderInfo(const FString& SenderName, FSpoutSenderInfo& OutInfo) const;

	UFUNCTION(BlueprintCallable, Category = "Spout")
	bool IsSenderAvailable(const FString& SenderName) const;

	/** The sender Spout considers active machine-wide; receivers with no name follow it. */
	UFUNCTION(BlueprintCallable, Category = "Spout")
	FString GetActiveSenderName() const;

	UFUNCTION(BlueprintCallable, Category = "Spout")
	bool SetActiveSenderName(const FString& SenderName);

	/** Forces an immediate refresh instead of waiting for the next poll. */
	UFUNCTION(BlueprintCallable, Category = "Spout")
	void RefreshSenders();

	/** Fires when a sender appears. */
	UPROPERTY(BlueprintAssignable, Category = "Spout")
	FSpoutSenderEvent OnSenderAdded;

	/** Fires when a sender goes away. Only the Name field is meaningful. */
	UPROPERTY(BlueprintAssignable, Category = "Spout")
	FSpoutSenderEvent OnSenderRemoved;

	/** Fires once per refresh in which anything changed. */
	UPROPERTY(BlueprintAssignable, Category = "Spout")
	FSpoutSenderListEvent OnSenderListChanged;

private:
	bool Tick(float DeltaSeconds);

	UPROPERTY(Transient)
	TArray<FSpoutSenderInfo> Senders;

	FTSTicker::FDelegateHandle TickerHandle;
	float TimeSinceRefresh = 0.f;
};
