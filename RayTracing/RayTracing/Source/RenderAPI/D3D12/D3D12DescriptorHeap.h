#pragma once

/**
 * @brief Wrapper for D3D12 descriptor heap.
 */
class D3D12DescriptorHeap
{
private:
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDescriptorHeap;
public:
	/**
	 * @brief Creates the descriptor heap.
	 * @param pDevice D3D12 device.
	 * @param heapDesc Descriptor heap description.
	 */
	void Initialize(ID3D12Device* pDevice, const D3D12_DESCRIPTOR_HEAP_DESC& heapDesc);
	/**
	 * @brief Returns the native descriptor heap pointer.
	 */
	ID3D12DescriptorHeap* Get() const { return mDescriptorHeap.Get(); }
};

