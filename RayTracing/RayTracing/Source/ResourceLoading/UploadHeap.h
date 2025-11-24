#pragma once
#include "RenderAPI/D3D12/D3D12Resource.h"
#include "helpers.h"

/**
 * @brief Linear upload heap for staging buffer/texture data with sub-allocation.
 */
class UploadHeap
{
private:
    D3D12Resource mResource;          ///< Underlying UPLOAD heap buffer resource.
    uint8_t* mMapped = nullptr;       ///< Persistently mapped CPU pointer to heap data.
    uint64_t mSize = 0;               ///< Total size in bytes of the upload heap.
    uint64_t mOffset = 0;             ///< Current allocation offset pointer.
public:
    /**
     * @brief Represents one sub-allocation returned to the caller.
     */
    struct Allocation
    {
        uint64_t offset = 0; ///< Offset into upload resource where data begins.
        void* cpuPtr = nullptr; ///< CPU pointer to write data at the allocation.
    };

    /**
     * @brief Initializes and maps an upload heap of the requested size.
     * @param device D3D12 device.
     * @param sizeBytes Size in bytes to allocate.
     */
    void Initialize(ID3D12Device* device, uint64_t sizeBytes);

    /**
     * @brief Resets the linear offset to start reusing memory (caller must ensure GPU finished).
     */
    void Reset()
    {
        mOffset = 0; // Simple ring reset per frame (caller must ensure GPU completed usage)
    }

    /**
     * @brief Allocates a chunk of memory with alignment from the upload heap.
     * @param size Size in bytes required.
     * @param alignment Alignment in bytes (default 256 for constant buffer/texture requirements).
     * @return Allocation info (empty if out of space).
     */
    Allocation Allocate(uint64_t size, uint64_t alignment = 256);

    /**
     * @brief Returns the underlying upload heap resource pointer.
     */
    ID3D12Resource* GetResource() const { return mResource.Get(); }
};
