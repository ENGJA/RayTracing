#pragma once
#include "DepthDescHeap.h"
#include "RenderAPI/D3D12/D3D12Resource.h"
class DepthBuffer
{
private:
	D3D12Resource mDepthStencilBuffer;
	DepthDescHeap mDescHeap;
	//D3D12_CPU_DESCRIPTOR_HANDLE mDSVHandle{};

public:
	void Initialize(ID3D12Device* pDevice, UINT width, UINT height);//, D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle)
	ID3D12Resource* GetResource() const { return mDepthStencilBuffer.Get(); }
	D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle() const { return mDescHeap.GetDSVHandle(); }
};

