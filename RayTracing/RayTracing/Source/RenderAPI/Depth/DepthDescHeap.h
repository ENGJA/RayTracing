#pragma once
#include "RenderAPI/D3D12/D3D12DescriptorHeap.h"
class DepthDescHeap
{
private:
	D3D12DescriptorHeap mDescriptorHeap;

public:
	void Initialize(ID3D12Device* pDevice);
	D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle() const
	{
		return mDescriptorHeap.Get()->GetCPUDescriptorHandleForHeapStart();
	}
};

