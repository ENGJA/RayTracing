#include "pch.h"
#include "D3D12Resource.h"
#include "helpers.h"

void D3D12Resource::Initialize(ID3D12Device* device, unsigned int numBytes, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES initialState)
{
	D3D12_HEAP_PROPERTIES heapProperties{};
	heapProperties.Type = heapType;
	heapProperties.CPUPageProperty = D3D12_CPU_PAGE_PROPERTY_UNKNOWN;
	heapProperties.MemoryPoolPreference = D3D12_MEMORY_POOL_UNKNOWN;
	heapProperties.CreationNodeMask = 1;
	heapProperties.VisibleNodeMask = 1;

	D3D12_RESOURCE_DESC resourceDesc{};
	resourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	resourceDesc.Alignment = 0;
	resourceDesc.Width = numBytes;
	resourceDesc.Height = 1;
	resourceDesc.DepthOrArraySize = 1;
	resourceDesc.MipLevels = 1;
	resourceDesc.Format = DXGI_FORMAT_UNKNOWN;
	resourceDesc.SampleDesc.Count = 1;
	resourceDesc.SampleDesc.Quality = 0;
	resourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
	resourceDesc.Flags = D3D12_RESOURCE_FLAG_NONE;


	HRESULT hr = device->CreateCommittedResource(
		&heapProperties,
		D3D12_HEAP_FLAG_NONE,
		&resourceDesc,
		initialState,
		nullptr,
		IID_PPV_ARGS(&mResource)
	);

	ASSERT_HR(hr, "Failed to create D3D12 resource.");
}
