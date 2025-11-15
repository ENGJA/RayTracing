#pragma once
#include <wrl.h>
#include <d3d12.h>

struct DescriptorAllocation
{
    D3D12_CPU_DESCRIPTOR_HANDLE cpuHandle{};
    D3D12_GPU_DESCRIPTOR_HANDLE gpuHandle{};
    UINT index = UINT_MAX;
};

class ShaderVisibleDescriptorHeap
{
private:
    Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mHeap;
    UINT mIncrementSize = 0;
    UINT mCapacity = 0;
    UINT mAllocated = 0;
public:
    void Initialize(ID3D12Device* device, UINT capacity);
    DescriptorAllocation Allocate(UINT count = 1);
    ID3D12DescriptorHeap* Get() const { return mHeap.Get(); }
    UINT GetIncrementSize() const { return mIncrementSize; }
};
