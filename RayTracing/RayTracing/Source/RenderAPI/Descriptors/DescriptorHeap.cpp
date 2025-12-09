#include "pch.h"
#include "DescriptorHeap.h"
#include "helpers.h"

//void ShaderVisibleDescriptorHeap::Initialize(ID3D12Device* pDevice, UINT capacity)
//{
//    mCapacity = capacity;
//    D3D12_DESCRIPTOR_HEAP_DESC desc{};
//    desc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
//    desc.NumDescriptors = capacity;
//    desc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
//    mHeap.Initialize(pDevice, desc);
//    mIncrementSize = pDevice->GetDescriptorHandleIncrementSize(desc.Type);
//}

DescriptorAllocation DescriptorHeap::Allocate(UINT count)
{
    DescriptorAllocation alloc{};
    if (mAllocated + count > mCapacity) 
        return alloc;

    alloc.index = mAllocated;
	alloc.cpuHandle = GetCpuHandle(mAllocated); 
	alloc.gpuHandle = GetGpuHandle(mAllocated);

    mAllocated += count;
    return alloc;
}

void DescriptorHeap::Initialize(ID3D12Device* pDevice, UINT capacity, D3D12_DESCRIPTOR_HEAP_TYPE type, bool shaderVisible)
{
    mCapacity = capacity;
    mShaderVisible = shaderVisible;
    D3D12_DESCRIPTOR_HEAP_DESC desc{};
    desc.Type = type;
    desc.NumDescriptors = capacity;
	desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    mHeap.Initialize(pDevice, desc);
	mIncrementSize = pDevice->GetDescriptorHandleIncrementSize(desc.Type);
}

D3D12_CPU_DESCRIPTOR_HANDLE DescriptorHeap::GetCpuHandle(UINT index) const
{    
    D3D12_CPU_DESCRIPTOR_HANDLE handle = mHeap.Get()->GetCPUDescriptorHandleForHeapStart();
    handle.ptr += SIZE_T(index) * mIncrementSize;
    return handle;    
}

D3D12_GPU_DESCRIPTOR_HANDLE DescriptorHeap::GetGpuHandle(UINT index) const
{
    if (!mShaderVisible)
        return { 0 };

    D3D12_GPU_DESCRIPTOR_HANDLE handle = mHeap.Get()->GetGPUDescriptorHandleForHeapStart();
    handle.ptr += UINT64(index) * mIncrementSize;
    return handle;
}
