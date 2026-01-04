#include "pch.h"
#include "DescriptorHeap.h"

void DescriptorHeap::Initialize(ID3D12Device* pDevice, D3D12_DESCRIPTOR_HEAP_TYPE type, UINT capacity, bool shaderVisible)
{
	mCapacity = capacity;
	mShaderVisible = shaderVisible;

	D3D12_DESCRIPTOR_HEAP_DESC desc{};
	desc.Type = type;
	desc.NumDescriptors = capacity;
	desc.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	mHeap.Initialize(pDevice, desc);
	mIncrementSize = pDevice->GetDescriptorHandleIncrementSize(type);
}

DescriptorHandle DescriptorHeap::Allocate(UINT count)
{
	DescriptorHandle alloc{};
	if (mAllocated + count > mCapacity) 
		return alloc;

	alloc.index = mAllocated;
	alloc.cpuHandle = GetCpuHandle(mAllocated);
	alloc.gpuHandle = GetGpuHandle(mAllocated);

	mAllocated += count;
	return alloc;
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
		return {0};

	D3D12_GPU_DESCRIPTOR_HANDLE handle = mHeap.Get()->GetGPUDescriptorHandleForHeapStart();
	handle.ptr += UINT64(index) * mIncrementSize;
	return handle;
}
