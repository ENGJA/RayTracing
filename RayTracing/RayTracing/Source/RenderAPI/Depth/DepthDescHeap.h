#pragma once
#include "RenderAPI/D3D12/D3D12DescriptorHeap.h"

/**
 * @brief Descriptor heap wrapper for a single DSV heap.
 */
class DepthDescHeap
{
private:
	D3D12DescriptorHeap mDescriptorHeap; ///< Underlying DSV descriptor heap.

public:
	/**
	 * @brief Creates a DSV descriptor heap with one descriptor.
	 * @param pDevice D3D12 device.
	 */
	void Initialize(ID3D12Device* pDevice);
	/**
	 * @brief Returns the CPU handle to the first descriptor in the heap.
	 */
	D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle() const
	{
		return mDescriptorHeap.Get()->GetCPUDescriptorHandleForHeapStart();
	}
};

