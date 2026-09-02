// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutD3D12Device.h"

#if PLATFORM_WINDOWS

#include "SpoutUE.h"
#include "RHI.h"
#include "RHIResources.h"
#include "ID3D12DynamicRHI.h"

TUniquePtr<FSpoutD3D12Device> FSpoutD3D12Device::Create()
{
	if (!GDynamicRHI || RHIGetInterfaceType() != ERHIInterfaceType::D3D12)
	{
		UE_LOG(LogSpout, Error,
			TEXT("Spout needs the D3D12 RHI. The current RHI is '%s'. Launch with -d3d12, or set it in Project Settings > Platforms > Windows > Default RHI."),
			GDynamicRHI ? GDynamicRHI->GetName() : TEXT("none"));
		return nullptr;
	}

	ID3D12DynamicRHI* DynamicRHI = GetID3D12DynamicRHI();
	if (!DynamicRHI)
	{
		UE_LOG(LogSpout, Error, TEXT("Could not reach the D3D12 dynamic RHI interface."));
		return nullptr;
	}

	ID3D12Device* Device12 = DynamicRHI->RHIGetDevice(0);
	ID3D12CommandQueue* Queue = DynamicRHI->RHIGetCommandQueue();
	if (!Device12 || !Queue)
	{
		UE_LOG(LogSpout, Error, TEXT("Unreal's D3D12 device or graphics queue was not available."));
		return nullptr;
	}

	TRefCountPtr<ID3D11Device> Device11;
	TRefCountPtr<ID3D11DeviceContext> Context11;

	// Passing Unreal's queue is what makes the 11On12 copies ordered against
	// Unreal's own rendering rather than racing it on a queue of their own.
	IUnknown* QueueAsUnknown = static_cast<IUnknown*>(Queue);

	const HRESULT CreateResult = D3D11On12CreateDevice(
		Device12,
		D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		nullptr,			// let the runtime pick the feature levels
		0,
		&QueueAsUnknown,
		1,
		0,					// node mask: single GPU, which is all Spout supports anyway
		Device11.GetInitReference(),
		Context11.GetInitReference(),
		nullptr);

	if (FAILED(CreateResult))
	{
		UE_LOG(LogSpout, Error, TEXT("D3D11On12CreateDevice failed (hr = 0x%08X)."), static_cast<uint32>(CreateResult));
		return nullptr;
	}

	TRefCountPtr<ID3D11On12Device> On12Device;
	const HRESULT QueryResult = Device11->QueryInterface(__uuidof(ID3D11On12Device), reinterpret_cast<void**>(On12Device.GetInitReference()));
	if (FAILED(QueryResult) || !On12Device.IsValid())
	{
		UE_LOG(LogSpout, Error, TEXT("Could not query ID3D11On12Device (hr = 0x%08X)."), static_cast<uint32>(QueryResult));
		return nullptr;
	}

	TUniquePtr<FSpoutD3D12Device> Result(new FSpoutD3D12Device());
	Result->D3D12Device = Device12;
	Result->D3D11Device = MoveTemp(Device11);
	Result->D3D11Context = MoveTemp(Context11);
	Result->D3D11On12Device = MoveTemp(On12Device);

	UE_LOG(LogSpout, Log, TEXT("D3D11On12 bridge created on Unreal's D3D12 device."));
	return Result;
}

FSpoutD3D12Device::~FSpoutD3D12Device()
{
	// Anything still queued on the D3D11 side has to land before the runtime goes
	// away, otherwise the D3D12 resources it references can outlive their owners.
	if (D3D11Context.IsValid())
	{
		D3D11Context->Flush();
		D3D11Context->ClearState();
	}

	D3D11On12Device.SafeRelease();
	D3D11Context.SafeRelease();
	D3D11Device.SafeRelease();
	D3D12Device = nullptr;
}

bool FSpoutD3D12Device::WrapResource(
	ID3D12Resource* InResource,
	D3D12_RESOURCE_STATES InState,
	D3D12_RESOURCE_STATES OutState,
	TRefCountPtr<ID3D11Resource>& OutWrapped) const
{
	OutWrapped.SafeRelease();

	if (!InResource || !D3D11On12Device.IsValid())
	{
		return false;
	}

	// Leaving the flags empty lets 11On12 infer bind flags from the D3D12 resource
	// description, which is what we want: the callers only ever copy.
	D3D11_RESOURCE_FLAGS Flags = {};

	const HRESULT Result = D3D11On12Device->CreateWrappedResource(
		InResource,
		&Flags,
		InState,
		OutState,
		IID_PPV_ARGS(OutWrapped.GetInitReference()));

	if (FAILED(Result))
	{
		UE_LOG(LogSpout, Error, TEXT("CreateWrappedResource failed (hr = 0x%08X)."), static_cast<uint32>(Result));
		OutWrapped.SafeRelease();
		return false;
	}

	return true;
}

void FSpoutD3D12Device::AcquireWrapped(ID3D11Resource* Wrapped) const
{
	if (Wrapped && D3D11On12Device.IsValid())
	{
		D3D11On12Device->AcquireWrappedResources(&Wrapped, 1);
	}
}

void FSpoutD3D12Device::ReleaseWrapped(ID3D11Resource* Wrapped) const
{
	if (Wrapped && D3D11On12Device.IsValid())
	{
		D3D11On12Device->ReleaseWrappedResources(&Wrapped, 1);
	}
}

void FSpoutD3D12Device::FlushD3D11() const
{
	if (D3D11Context.IsValid())
	{
		D3D11Context->Flush();
	}
}

ID3D12Resource* FSpoutD3D12Device::GetNativeResource(FRHITexture* Texture)
{
	if (!Texture || !GDynamicRHI || RHIGetInterfaceType() != ERHIInterfaceType::D3D12)
	{
		return nullptr;
	}

	return GetID3D12DynamicRHI()->RHIGetResource(Texture);
}

#endif // PLATFORM_WINDOWS
