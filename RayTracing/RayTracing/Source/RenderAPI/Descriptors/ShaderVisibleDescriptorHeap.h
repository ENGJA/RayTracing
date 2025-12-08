#pragma once
#include "RenderAPI/D3D12/D3D12DescriptorHeap.h"

/**
 * @brief Descriptor allocation metadata with CPU/GPU handles and heap index.
 */
struct DescriptorAllocation
{
    /** < CPU descriptor handle for the first descriptor. */
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
    /** < GPU descriptor handle for the first descriptor (visible heaps). */
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
    /** < Index within the descriptor heap. */
    UINT index = UINT_MAX;

    D3D12_CPU_DESCRIPTOR_HANDLE GetCpuHandle(UINT offset, UINT descriptorIncrementSize) const
    {
        D3D12_CPU_DESCRIPTOR_HANDLE handle = cpuHandle;
        handle.ptr += static_cast<SIZE_T>(offset) * descriptorIncrementSize;
        return handle;
    }

    D3D12_GPU_DESCRIPTOR_HANDLE GetGpuHandle(UINT offset, UINT descriptorIncrementSize) const
    {
        D3D12_GPU_DESCRIPTOR_HANDLE handle = gpuHandle;
        handle.ptr += static_cast<UINT64>(offset) * descriptorIncrementSize;
        return handle;
    }
};

/**
 * @brief Simple shader-visible CBV/SRV/UAV descriptor heap manager with linear allocations.
 */
class ShaderVisibleDescriptorHeap
{
private:
    D3D12DescriptorHeap mHeap; ///< Underlying descriptor heap COM pointer.
    UINT mIncrementSize = 0; ///< Descriptor handle increment size for this heap type.
    UINT mCapacity = 0; ///< Total number of descriptors available.
    UINT mAllocated = 0; ///< Number of descriptors already allocated linearly.
public:
    /**
     * @brief Creates a shader-visible descriptor heap with the given capacity.
     * @param device D3D12 device.
     * @param capacity Number of descriptors in the heap.
     */
    void Initialize(ID3D12Device* device, UINT capacity);
    /**
     * @brief Allocates a consecutive range of descriptors.
     * @param count Number of descriptors to allocate.
     * @return Allocation info with CPU/GPU handles and heap index.
     */
    DescriptorAllocation Allocate(UINT count = 1);
    /**
     * @brief Returns the native heap pointer.
     */
    ID3D12DescriptorHeap* Get() const { return mHeap.Get(); }
    /**
     * @brief Returns the descriptor size increment for this heap type.
     */
    UINT GetIncrementSize() const { return mIncrementSize; }
};
