// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpoutUETypes.h"
#include "SpoutSenderComponent.generated.h"

class UTextureRenderTarget2D;
class FSpoutSenderStream;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FSpoutSenderStateEvent, const FString&, SenderName);

/**
 * Publishes a render target as a Spout sender.
 *
 * Attach it to any actor, point it at a render target and the contents are
 * shared with every Spout application on the machine, with no compression and
 * without ever leaving the GPU. Works in the editor viewport as well as in PIE
 * and packaged builds, depending on the startup policy.
 */
UCLASS(ClassGroup = (Spout), meta = (BlueprintSpawnableComponent, DisplayName = "Spout Sender"))
class SPOUTUE_API USpoutSenderComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USpoutSenderComponent();

	//~ UActorComponent
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif

	/**
	 * Name this sender is published under.
	 *
	 * If another process already owns the name, Spout appends a suffix and the
	 * real name shows up in GetPublishedSenderName.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spout|Sender")
	FString SenderName;

	/** The render target whose contents are shared. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Spout|Sender")
	TObjectPtr<UTextureRenderTarget2D> SourceRenderTarget;

	/** Format of the shared texture. 8 bit BGRA is the safest choice for wide compatibility. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout|Sender")
	ESpoutPixelFormat PixelFormat = ESpoutPixelFormat::Auto;

	/**
	 * Transfer function applied on the way out.
	 *
	 * Unreal render targets hold linear values, so "match viewport" is what makes
	 * the shared frame look like what you see in the editor.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout|Sender")
	ESpoutColorSpace ColorSpace = ESpoutColorSpace::MatchViewport;

	/** How the alpha channel is written into the shared texture. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout|Sender")
	ESpoutAlphaMode AlphaMode = ESpoutAlphaMode::Opaque;

	/** Flips the frame vertically. A few older receivers expect bottom-up images. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout|Sender")
	bool bFlipVertical = false;

	/** When the sender should bring itself up without an explicit StartSending call. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout|Sender")
	ESpoutStartupPolicy StartupPolicy = ESpoutStartupPolicy::GameOnly;

	/**
	 * Cap on how often frames are published, in frames per second.
	 *
	 * Zero publishes on every tick. Capping is worth it when Unreal runs far
	 * faster than the receiver needs, since each published frame costs a copy.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spout|Sender", meta = (ClampMin = "0.0", ClampMax = "240.0"))
	float MaxFrameRate = 0.f;

	/** Starts publishing. Safe to call when already sending. */
	UFUNCTION(BlueprintCallable, Category = "Spout|Sender")
	void StartSending();

	/** Stops publishing and removes the sender from the Spout directory. */
	UFUNCTION(BlueprintCallable, Category = "Spout|Sender")
	void StopSending();

	UFUNCTION(BlueprintCallable, Category = "Spout|Sender")
	bool IsSending() const;

	/** Renaming a live sender restarts it, since Spout identifies senders by name. */
	UFUNCTION(BlueprintCallable, Category = "Spout|Sender")
	void SetSenderName(const FString& NewName);

	UFUNCTION(BlueprintCallable, Category = "Spout|Sender")
	void SetSourceRenderTarget(UTextureRenderTarget2D* NewRenderTarget);

	/** The name Spout actually published, which may carry a _1, _2... suffix. */
	UFUNCTION(BlueprintCallable, Category = "Spout|Sender")
	FString GetPublishedSenderName() const;

	UFUNCTION(BlueprintCallable, Category = "Spout|Sender")
	FIntPoint GetPublishedSize() const;

	UFUNCTION(BlueprintCallable, Category = "Spout|Sender")
	FSpoutStreamStats GetStats() const;

	/** Fires the first time a frame is accepted by Spout. */
	UPROPERTY(BlueprintAssignable, Category = "Spout|Sender")
	FSpoutSenderStateEvent OnSenderStarted;

	UPROPERTY(BlueprintAssignable, Category = "Spout|Sender")
	FSpoutSenderStateEvent OnSenderStopped;

protected:
	/** True when the startup policy says this context should be publishing. */
	bool ShouldAutoStart() const;

	/** Pushes one frame down to the render thread. */
	void SendFrame();

	/** Destroys the stream on the render thread, which is the only place it may be touched. */
	void DestroyStream();

	UPROPERTY(Transient)
	bool bIsSending = false;

private:
	/** Owned by the game thread, used only on the render thread. */
	FSpoutSenderStream* Stream = nullptr;

	FString LastPublishedName;
	double LastSendTimeSeconds = 0.0;
};
