// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutCineCameraActor.h"

#include "CineCameraComponent.h"
#include "Engine/TextureRenderTarget2D.h"
#include "SpoutCaptureComponent.h"
#include "SpoutSenderComponent.h"

ASpoutCineCameraActor::ASpoutCineCameraActor(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	// Keeping the wiring live in the editor is the point of this actor: you frame
	// the shot and the output follows, without entering PIE.
	SetActorTickEnabled(true);

	SpoutCapture = CreateDefaultSubobject<USpoutCaptureComponent>(TEXT("SpoutCapture"));
	if (SpoutCapture)
	{
		SpoutCapture->SetupAttachment(GetCineCameraComponent());
	}

	SpoutSender = CreateDefaultSubobject<USpoutSenderComponent>(TEXT("SpoutSender"));
	if (SpoutSender)
	{
		SpoutSender->SenderName = TEXT("UnrealCineCam");
	}
}

void ASpoutCineCameraActor::BeginPlay()
{
	Super::BeginPlay();
	SyncSenderTarget();
}

void ASpoutCineCameraActor::SyncSenderTarget()
{
	if (!SpoutCapture || !SpoutSender)
	{
		return;
	}

	// The capture rebuilds its target on a resolution or format change, so the
	// sender has to be re-pointed rather than wired up once.
	UTextureRenderTarget2D* CaptureTarget = SpoutCapture->GetCaptureTarget();
	if (SpoutSender->SourceRenderTarget != CaptureTarget)
	{
		SpoutSender->SetSourceRenderTarget(CaptureTarget);
	}
}

void ASpoutCineCameraActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	SyncSenderTarget();
}
