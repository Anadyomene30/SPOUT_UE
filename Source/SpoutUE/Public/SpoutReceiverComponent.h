// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpoutUETypes.h"
#include "SpoutReceiverComponent.generated.h"

class UTextureRenderTarget2D;
class FSpoutReceiverStream;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSpoutReceiverConnectedEvent, const FSpoutSenderInfo&, SenderInfo);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSpoutReceiverDisconnectedEvent, const FString&, SenderName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSpoutReceiverResizedEvent, FIntPoint, NewSize);

/**
 * Pulls a Spout sender into a render target.
 *
 * Point a material, a Composure layer or anything else that reads a texture at
 * TargetRenderTarget and it updates every frame the sender produces one. Leave
 * SenderName empty to follow whichever sender Spout considers active, which is
 * the usual way to let an operator switch sources from outside Unreal.
 */
UCLASS(ClassGroup = (Spout), meta = (BlueprintSpawnableComponent, DisplayName = "Spout Receiver"))
class SPOUTUE_API USpoutReceiverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USpoutReceiverComponent();

	//~ UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/** Sender to connect to. Empty follows Spout's machine-wide active sender. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spout|Receiver")
	FString SenderName;

	/**
	 * Where received frames are written.
	 *
	 * Leave it empty with bAutoCreateRenderTarget on and the component builds one
	 * that matches the sender, which is the quickest way to get a feed onto a
	 * material without pre-authoring an asset.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout|Receiver")
	TObjectPtr<UTextureRenderTarget2D> TargetRenderTarget;

	/** Creates a transient render target matching the sender when none is assigned. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout|Receiver")
	bool bAutoCreateRenderTarget = true;

	/** Resizes the render target whenever the sender changes size or format. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout|Receiver")
	bool bAutoResizeRenderTarget = true;

	/**
	 * Transfer function applied on the way in.
	 *
	 * Most Spout senders publish encoded (sRGB) frames, so "to linear" is what
	 * makes them composite correctly against an Unreal scene.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout|Receiver")
	ESpoutColorSpace ColorSpace = ESpoutColorSpace::ToLinear;

	/** How the incoming alpha channel is interpreted. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout|Receiver")
	ESpoutAlphaMode AlphaMode = ESpoutAlphaMode::Straight;

	/** Flips the incoming frame vertically. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout|Receiver")
	bool bFlipVertical = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout|Receiver")
	ESpoutStartupPolicy StartupPolicy = ESpoutStartupPolicy::GameOnly;

	UFUNCTION(BlueprintCallable, Category = "Spout|Receiver")
	void StartReceiving();

	UFUNCTION(BlueprintCallable, Category = "Spout|Receiver")
	void StopReceiving();

	/** True once a sender has been found and is producing frames. */
	UFUNCTION(BlueprintCallable, Category = "Spout|Receiver")
	bool IsConnected() const;

	/** Switching sender restarts the connection. */
	UFUNCTION(BlueprintCallable, Category = "Spout|Receiver")
	void SetSenderName(const FString& NewName);

	/** Details of the sender currently connected. Invalid while disconnected. */
	UFUNCTION(BlueprintCallable, Category = "Spout|Receiver")
	FSpoutSenderInfo GetConnectedSenderInfo() const;

	UFUNCTION(BlueprintCallable, Category = "Spout|Receiver")
	UTextureRenderTarget2D* GetRenderTarget() const { return TargetRenderTarget; }

	UFUNCTION(BlueprintCallable, Category = "Spout|Receiver")
	FSpoutStreamStats GetStats() const;

	UPROPERTY(BlueprintAssignable, Category = "Spout|Receiver")
	FSpoutReceiverConnectedEvent OnConnected;

	UPROPERTY(BlueprintAssignable, Category = "Spout|Receiver")
	FSpoutReceiverDisconnectedEvent OnDisconnected;

	/** Fires when the sender changes size, after the render target has been resized. */
	UPROPERTY(BlueprintAssignable, Category = "Spout|Receiver")
	FSpoutReceiverResizedEvent OnSenderResized;

protected:
	bool ShouldAutoStart() const;
	void ReceiveFrame();
	void DestroyStream();

	/** Applies whatever the render thread learned last frame: resize, events, state. */
	void ProcessLastResult();

	/** Builds or resizes the transient render target to match the sender. */
	void ConformRenderTarget(FIntPoint Size, EPixelFormat Format);

	UPROPERTY(Transient)
	bool bIsReceiving = false;

	UPROPERTY(Transient)
	FSpoutSenderInfo ConnectedSender;

private:
	FSpoutReceiverStream* Stream = nullptr;

	bool bWasConnected = false;
	FIntPoint LastConformedSize = FIntPoint::ZeroValue;
	EPixelFormat LastConformedFormat = PF_Unknown;
};
