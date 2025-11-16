#pragma once
#include <cstdint>
#include <stdexcept>
#include <iostream>
#include "D3D12Resource.h"
#include "helpers.h"

/**
 * @brief Linear upload heap for staging buffer/texture data with sub-allocation.
 */
class UploadHeap
{
private:
    D3D12Resource mResource;          // Underlying UPLOAD heap buffer
    uint8_t* mMapped = nullptr;       // Persistently mapped CPU pointer
    uint64_t mSize = 0;               // Total size in bytes
    uint64_t mOffset = 0;             // Current allocation offset
public:
    struct Allocation
    {
        uint64_t offset = 0; // Offset into upload resource
        void* cpuPtr = nullptr; // Pointer to write data
    };

    void Initialize(ID3D12Device* device, uint64_t sizeBytes)
    {
        mSize = sizeBytes;
        mResource.Initialize(device, (unsigned int)sizeBytes, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
        // Map once
        D3D12_RANGE range{ 0, 0 };
        void* ptr = nullptr;
        HRESULT hr = mResource.Get()->Map(0, &range, &ptr);
		ASSERT_HR(hr, "Failed to map upload heap.");
        //if (FAILED(hr)) throw std::runtime_error("Failed to map upload heap.");
        mMapped = reinterpret_cast<uint8_t*>(ptr);
        mOffset = 0;
    }

    void Reset()
    {
        mOffset = 0; // Simple ring reset per frame (caller must ensure GPU completed usage)
    }

    Allocation Allocate(uint64_t size, uint64_t alignment = 256)
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

    ID3D12Resource* GetResource() const { return mResource.Get(); }
};
