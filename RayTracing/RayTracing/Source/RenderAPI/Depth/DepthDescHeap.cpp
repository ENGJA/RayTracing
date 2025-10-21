#include "pch.h"
#include "DepthDescHeap.h"

void DepthDescHeap::Initialize(ID3D12Device* pDevice)
{
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	heapDesc.NumDescriptors = 1;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	mDescriptorHeap.Initialize(pDevice, heapDesc);
}
