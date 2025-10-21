#pragma once
class D3D12DescriptorHeap
{
private:
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDescriptorHeap;
public:
	void Initialize(ID3D12Device* pDevice, const D3D12_DESCRIPTOR_HEAP_DESC& heapDesc);
	ID3D12DescriptorHeap* Get() const { return mDescriptorHeap.Get(); }
};

