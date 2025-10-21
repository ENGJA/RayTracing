#pragma once
class D3D12Resource
{
private:
	Microsoft::WRL::ComPtr<ID3D12Resource> mResource;

public:
	ID3D12Resource* Get() const { return mResource.Get(); }
	void Initialize(ID3D12Device* pDevice, const unsigned int numBytes, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES initialState);

	void Initialize(ID3D12Device* pDevice, const D3D12_RESOURCE_DESC& resourceDesc, D3D12_HEAP_TYPE heapType, D3D12_RESOURCE_STATES initialState, const D3D12_CLEAR_VALUE* clearValue = nullptr);
};

