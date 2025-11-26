#include "pch.h"
#include "UploadHeap.h"
#include "helpers.h"

void UploadHeap::Initialize(ID3D12Device* device, uint64_t sizeBytes)
{
    mSize = sizeBytes;
    mResource.Initialize(device, (unsigned int)sizeBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
    // Map once
    D3D12_RANGE range{ 0, 0 };
    void* ptr = nullptr;
    HRESULT hr = mResource.Get()->Map(0, &range, &ptr);
    ASSERT_HR(hr, "Failed to map upload heap.");
    mMapped = reinterpret_cast<uint8_t*>(ptr);
    mOffset = 0;
}

UploadHeap::Allocation UploadHeap::Allocate(uint64_t size, uint64_t alignment)
{
    // Align offset
    uint64_t aligned = (mOffset + (alignment - 1)) & ~(alignment - 1);
    if (aligned + size > mSize) return {}; // Out of space
    Allocation alloc;
    alloc.offset = aligned;
    alloc.cpuPtr = mMapped + aligned;
    mOffset = aligned + size;
    return alloc;
}

bool UploadHeap::CanAllocate(size_t size) const
{
    return (mOffset + size) <= mSize; // mOffset – bie¿¹cy pointer, mSize – ca³kowita pojemnoœæ
}

bool UploadHeap::CanAllocate(uint64_t size, uint64_t alignment) const
{
    const uint64_t aligned = (mOffset + (alignment - 1)) & ~(alignment - 1);
    return (aligned + size) <= mSize;
}