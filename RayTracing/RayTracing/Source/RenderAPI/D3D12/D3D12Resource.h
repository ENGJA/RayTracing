#pragma once
class D3D12Resource
{
private:
	Microsoft::WRL::ComPtr<ID3D12Resource> mResource;

public:
	ID3D12Resource* GetResource() const { return mResource.Get(); }
	void Initialize(ID3D12Device* device, const unsigned int numBytes, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES initialState);
};

