// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "CineCameraActor.h"
#include "SpoutCineCameraActor.generated.h"

class USpoutCaptureComponent;
class USpoutSenderComponent;

/**
 * A CineCamera whose image goes straight out over Spout.
 *
 * It is an ordinary ACineCameraActor in every respect, so filmback, focal
 * length and focus behave exactly as they do on any cine camera and the shot
 * can be worked on in Sequencer as usual. What it adds is a capture component
 * that renders the camera into its own render target at broadcast resolution,
 * and a sender that publishes it.
 *
 * Several of these can live in a level at once; that is the multicam case, and
 * each one can be disabled individually to hand its cost back to the GPU.
 */
UCLASS(ClassGroup = (Spout), meta = (DisplayName = "Spout Cine Camera"))
class SPOUTUE_API ASpoutCineCameraActor : public ACineCameraActor
{
	GENERATED_BODY()

public:
	ASpoutCineCameraActor(const FObjectInitializer& ObjectInitializer);

	//~ AActor
	virtual void Tick(float DeltaSeconds) override;
	virtual void BeginPlay() override;

	UFUNCTION(BlueprintPure, Category = "Spout")
	USpoutCaptureComponent* GetSpoutCapture() const { return SpoutCapture; }

	UFUNCTION(BlueprintPure, Category = "Spout")
	USpoutSenderComponent* GetSpoutSender() const { return SpoutSender; }

protected:
	/** Keeps the sender pointed at whatever target the capture currently owns. */
	void SyncSenderTarget();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spout", meta = (ShowOnlyInnerProperties))
	TObjectPtr<USpoutCaptureComponent> SpoutCapture;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Spout", meta = (ShowOnlyInnerProperties))
	TObjectPtr<USpoutSenderComponent> SpoutSender;
};
