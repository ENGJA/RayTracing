#include "pch.h"
//#include "ShaderVisibleDescriptorHeap.h"
//#include "helpers.h"
//
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
//
//DescriptorAllocation ShaderVisibleDescriptorHeap::Allocate(UINT count)
//{
//    DescriptorAllocation alloc{};
//    if (mAllocated + count > mCapacity) return alloc;
//
//    auto cpuStart = mHeap.Get()->GetCPUDescriptorHandleForHeapStart();
//    auto gpuStart = mHeap.Get()->GetGPUDescriptorHandleForHeapStart();
//
//    alloc.index = mAllocated;
//    alloc.cpuHandle.ptr = cpuStart.ptr + SIZE_T(mAllocated) * mIncrementSize;
//    alloc.gpuHandle.ptr = gpuStart.ptr + UINT64(mAllocated) * mIncrementSize;
//    mAllocated += count;
//    return alloc;
//}
//