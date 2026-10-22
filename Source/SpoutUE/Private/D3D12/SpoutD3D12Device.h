// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "CoreMinimal.h"
#include "Templates/RefCounting.h"

#if PLATFORM_WINDOWS

#include "Windows/AllowWindowsPlatformTypes.h"
THIRD_PARTY_INCLUDES_START
#include <d3d11.h>
#include <d3d12.h>
#include <d3d11on12.h>
THIRD_PARTY_INCLUDES_END
#include "Windows/HideWindowsPlatformTypes.h"

class FRHICommandListImmediate;
class FRHITexture;

/**
 * The process-wide D3D11On12 bridge.
 *
 * Spout shares textures through the D3D11 sharing model: a sender publishes a
 * DXGI shared handle that receivers open with ID3D11Device::OpenSharedResource.
 * Those are legacy (non-NT) handles, which D3D12 cannot open directly, so the
 * only way to bring Unreal's D3D12 resources into that world is to layer a
 * D3D11On12 device on top of Unreal's own device and command queue and let the
 * D3D11 runtime do the copies.
 *
 * Because the 11On12 device is created against Unreal's graphics queue, every
 * copy it submits is ordered against Unreal's own work on that queue, so no
 * cross-queue fence of our own is needed. What still matters is that Unreal has
 * actually submitted the work that produced the frame before we copy it, which
 * is what RunOnGraphicsQueue_RenderThread guarantees.
 *
 * One instance is shared by every sender and receiver: a second 11On12 device
 * would mean a second D3D11 runtime, for no benefit.
 */
class FSpoutD3D12Device
{
public:
	~FSpoutD3D12Device();

	/** Builds the bridge on top of Unreal's D3D12 device. Returns null on failure or on a non-D3D12 RHI. */
	static TUniquePtr<FSpoutD3D12Device> Create();

	ID3D12Device*			GetD3D12Device() const { return D3D12Device; }
	ID3D11Device*			GetD3D11Device() const { return D3D11Device.GetReference(); }
	ID3D11DeviceContext*	GetD3D11Context() const { return D3D11Context.GetReference(); }
	ID3D11On12Device*		GetD3D11On12Device() const { return D3D11On12Device.GetReference(); }

	/**
	 * Creates a D3D11 view onto a D3D12 resource.
	 *
	 * InState is the state the D3D12 resource must be in when Acquire is called;
	 * OutState is the state it is left in by Release. Both must be the state the
	 * D3D12 RHI believes the resource is in, because the RHI never sees what
	 * 11On12 does; 11On12 issues its own barriers around its work in between.
	 */
	bool WrapResource(
		ID3D12Resource* InResource,
		D3D12_RESOURCE_STATES InState,
		D3D12_RESOURCE_STATES OutState,
		TRefCountPtr<ID3D11Resource>& OutWrapped) const;

	/** Hands a wrapped resource over to the D3D11 side. */
	void AcquireWrapped(ID3D11Resource* Wrapped) const;

	/** Hands a wrapped resource back to D3D12, transitioning it to its OutState. */
	void ReleaseWrapped(ID3D11Resource* Wrapped) const;

	/** Submits the D3D11 command list to the shared queue. */
	void FlushD3D11() const;

	/**
	 * Runs Work on Unreal's submission thread, after every command the render
	 * thread has recorded so far has been handed to the graphics queue, and
	 * before anything recorded afterwards is.
	 *
	 * All D3D11On12 work belongs in here. Recording it straight from the render
	 * thread puts it on the queue ahead of Unreal's own commands, which are still
	 * waiting on the RHI thread, so the copy reads a frame that is not drawn yet
	 * and the debug layer reports the resource in the wrong state.
	 *
	 * Work must only capture what it can keep alive by itself, plus objects whose
	 * teardown flushes the RHI thread first.
	 */
	static void RunOnGraphicsQueue_RenderThread(FRHICommandListImmediate& RHICmdList, TFunction<void()>&& Work);

	/** Native D3D12 resource behind an RHI texture, or null. */
	static ID3D12Resource* GetNativeResource(FRHITexture* Texture);

private:
	FSpoutD3D12Device() = default;

	/** Not owned: this is Unreal's device, we only hold a raw pointer to it. */
	ID3D12Device* D3D12Device = nullptr;

	TRefCountPtr<ID3D11Device> D3D11Device;
	TRefCountPtr<ID3D11DeviceContext> D3D11Context;
	TRefCountPtr<ID3D11On12Device> D3D11On12Device;
};

#endif // PLATFORM_WINDOWS
