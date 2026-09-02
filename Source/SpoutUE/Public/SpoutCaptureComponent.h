// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneCaptureComponent2D.h"
#include "SpoutUETypes.h"
#include "SpoutCaptureComponent.generated.h"

class FWidgetRenderer;
class UTextureRenderTarget2D;
class UUserWidget;

/**
 * Renders whatever it is attached to into a render target sized for output.
 *
 * Attach it to a CameraComponent or a CineCameraComponent and it inherits that
 * camera's transform and lens each frame, so a single capture component turns
 * any existing camera in the level into a live output without replacing it.
 *
 * The render target it manages is what a Spout sender is then pointed at. It is
 * created and resized on demand, and released entirely when the capture is
 * disabled, which is the cheapest way to get the GPU cost of an idle camera
 * back down to nothing.
 */
UCLASS(ClassGroup = (Spout), meta = (BlueprintSpawnableComponent, DisplayName = "Spout Capture"))
class SPOUTUE_API USpoutCaptureComponent : public USceneCaptureComponent2D
{
	GENERATED_BODY()

public:
	USpoutCaptureComponent();

	//~ UActorComponent
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Output resolution. Pick Custom to use CustomResolution instead. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spout|Capture")
	ESpoutResolution Resolution = ESpoutResolution::R1080p;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spout|Capture", meta = (EditCondition = "Resolution == ESpoutResolution::Custom", ClampMin = "1", ClampMax = "16384"))
	FIntPoint CustomResolution = FIntPoint(1920, 1080);

	/** Format of the capture target. 16 bit float keeps highlights that 8 bit would clip. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spout|Capture")
	ESpoutPixelFormat PixelFormat = ESpoutPixelFormat::BGRA8;

	/**
	 * Captures scene transparency into the alpha channel.
	 *
	 * Requires r.PostProcessing.PropagateAlpha to be enabled in the project, and
	 * warns once if it is not: without it Unreal writes an opaque alpha and the
	 * key you get downstream is a solid rectangle.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spout|Capture")
	bool bCaptureAlpha = false;

	/**
	 * Turns the capture off entirely and frees its render target.
	 *
	 * This is the switch to reach for on a multicam setup: an unused camera that
	 * is merely paused still holds its target, one that is disabled does not.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout|Capture")
	bool bCaptureEnabled = true;

	/** Freezes the capture on its last frame while keeping the target allocated. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout|Capture")
	bool bPaused = false;

	/** Captures at most this many times per second. Zero captures every tick. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout|Capture", meta = (ClampMin = "0.0", ClampMax = "240.0"))
	float MaxCaptureFrameRate = 0.f;

	/** Copies transform, field of view and post process from the camera this is attached to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout|Capture")
	bool bFollowAttachedCamera = true;

	/**
	 * Widget drawn on top of the captured frame, before it leaves Unreal.
	 *
	 * This is the burn-in: timecode, a lower third, a safe-area guide. It is
	 * composited into the capture target only, so it never appears in the game
	 * viewport.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spout|Burn-in")
	TSubclassOf<UUserWidget> BurnInWidgetClass;

	/** The render target the capture writes into. Point a Spout sender at this. */
	UFUNCTION(BlueprintCallable, Category = "Spout|Capture")
	UTextureRenderTarget2D* GetCaptureTarget() const { return CaptureTarget; }

	/** Resolved output size, taking the preset or the custom value. */
	UFUNCTION(BlueprintPure, Category = "Spout|Capture")
	FIntPoint GetCaptureSize() const;

	/** Live instance of the burn-in widget, for driving its properties from Blueprint. */
	UFUNCTION(BlueprintCallable, Category = "Spout|Burn-in")
	UUserWidget* GetBurnInWidget() const { return BurnInWidget; }

	/** Rebuilds the render target, for instance after changing resolution at runtime. */
	UFUNCTION(BlueprintCallable, Category = "Spout|Capture")
	void RebuildCaptureTarget();

protected:
	void ReleaseCaptureTarget();
	void ReleaseBurnIn();
	void EnsureBurnIn();
	void DrawBurnIn(float DeltaTime);
	void SyncWithAttachedCamera();

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> CaptureTarget;

	UPROPERTY(Transient)
	TObjectPtr<UTextureRenderTarget2D> BurnInTarget;

	UPROPERTY(Transient)
	TObjectPtr<UUserWidget> BurnInWidget;

private:
	/**
	 * Raw rather than a smart pointer: FWidgetRenderer is a deferred-cleanup
	 * object and has to be handed to BeginCleanup so the render thread is done
	 * with it before it is destroyed.
	 */
	FWidgetRenderer* BurnInRenderer = nullptr;

	double LastCaptureTimeSeconds = 0.0;
};
