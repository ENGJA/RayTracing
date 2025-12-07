#pragma once
#include "RenderAPI/D3D12/D3D12DescriptorHeap.h"


/**
 * @brief Descriptor allocation metadata with CPU/GPU handles and heap index.
 */
struct DescriptorHandle
{
    /** < CPU descriptor handle for the first descriptor. */
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
    /** < GPU descriptor handle for the first descriptor (visible heaps). */
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
    /** < Index within the descriptor heap. */
    UINT index = UINT_MAX;
};

class DescriptorHeap
{
	D3D12DescriptorHeap mHeap; ///< Underlying descriptor heap COM pointer.
	UINT mIncrementSize = 0; ///< Descriptor handle increment size for this heap type.
	UINT mCapacity = 0; ///< Total number of descriptors available.
	UINT mAllocated = 0; ///< Number of descriptors already allocated linearly.
	bool mShaderVisible = false;

public:
    /**
     * @brief Initializes the heap.
     * @param type The type (RTV, DSV, or CBV_SRV_UAV).
     * @param shaderVisible True for Texture/Constant heaps. False for RTV/DSV.
     */
    void Initialize(ID3D12Device* pDevice, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT capacity, bool shaderVisible);

    /**
     * @brief Allocates 'count' descriptors linearly.
     */
    DescriptorHandle Allocate(UINT count = 1);

    D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(UINT index) const;
    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(UINT index) const;

	ID3D12DescriptorHeap* Get() const { return mHeap.Get(); }

	void Reset() { mAllocated = 0; }
};

