// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "SpoutManagerActors.generated.h"

class UBillboardComponent;
class USpoutSenderComponent;
class USpoutReceiverComponent;

/**
 * Drop-in actor that publishes a render target over Spout.
 *
 * The component does all the work; this exists so that setting up an output is
 * a matter of dragging something into the level and picking a render target,
 * with no Blueprint required.
 */
UCLASS(ClassGroup = (Spout), meta = (DisplayName = "Spout Sender Manager"), HideCategories = (Input, Collision, Replication, Physics, HLOD, Networking, Cooking))
class SPOUTUE_API ASpoutSenderActor : public AActor
{
	GENERATED_BODY()

public:
	ASpoutSenderActor();

	UFUNCTION(BlueprintPure, Category = "Spout")
	USpoutSenderComponent* GetSender() const { return SenderComponent; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spout", meta = (ShowOnlyInnerProperties))
	TObjectPtr<USpoutSenderComponent> SenderComponent;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> Billboard;
#endif
};

/**
 * Drop-in actor that receives a Spout sender into a render target.
 */
UCLASS(ClassGroup = (Spout), meta = (DisplayName = "Spout Receiver Manager"), HideCategories = (Input, Collision, Replication, Physics, HLOD, Networking, Cooking))
class SPOUTUE_API ASpoutReceiverActor : public AActor
{
	GENERATED_BODY()

public:
	ASpoutReceiverActor();

	UFUNCTION(BlueprintPure, Category = "Spout")
	USpoutReceiverComponent* GetReceiver() const { return ReceiverComponent; }

protected:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spout", meta = (ShowOnlyInnerProperties))
	TObjectPtr<USpoutReceiverComponent> ReceiverComponent;

#if WITH_EDITORONLY_DATA
	UPROPERTY()
	TObjectPtr<UBillboardComponent> Billboard;
#endif
};
