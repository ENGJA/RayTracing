#include "pch.h"
#include "D3D12DescriptorHeap.h"
#include "helpers.h"

void D3D12DescriptorHeap::Initialize(ID3D12Device* pDevice, const D3D12_DESCRIPTOR_HEAP_DESC& heapDesc)
{
	HRESULT hr = pDevice->CreateDescriptorHeap(
		&heapDesc,
		IID_PPV_ARGS(mDescriptorHeap.ReleaseAndGetAddressOf())
	);

	ASSERT_HR(hr, "Failed to create D3D12 descriptor heap.");
}
