// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutManagerActors.h"

#include "Components/BillboardComponent.h"
#include "SpoutReceiverComponent.h"
#include "SpoutSenderComponent.h"

ASpoutSenderActor::ASpoutSenderActor()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	SenderComponent = CreateDefaultSubobject<USpoutSenderComponent>(TEXT("SpoutSender"));

#if WITH_EDITORONLY_DATA
	Billboard = CreateDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (Billboard)
	{
		Billboard->SetupAttachment(Root);
		Billboard->bIsScreenSizeScaled = true;
		Billboard->SetHiddenInGame(true);
	}
#endif
}

ASpoutReceiverActor::ASpoutReceiverActor()
{
	PrimaryActorTick.bCanEverTick = false;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	ReceiverComponent = CreateDefaultSubobject<USpoutReceiverComponent>(TEXT("SpoutReceiver"));

#if WITH_EDITORONLY_DATA
	Billboard = CreateDefaultSubobject<UBillboardComponent>(TEXT("Sprite"));
	if (Billboard)
	{
		Billboard->SetupAttachment(Root);
		Billboard->bIsScreenSizeScaled = true;
		Billboard->SetHiddenInGame(true);
	}
#endif
}
