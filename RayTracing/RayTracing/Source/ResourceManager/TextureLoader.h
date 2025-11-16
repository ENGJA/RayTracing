#pragma once
#include <string>
#include <vector>
#include "RenderAPI/D3D12/D3D12Resource.h"
#include "RenderAPI/Descriptors/ShaderVisibleDescriptorHeap.h"
#include "RenderAPI/D3D12/Command/D3D12CommandQueue.h"
#include "RenderAPI/D3D12/Command/D3D12CommandList.h"
#include "RenderAPI/D3D12/UploadHeap.h"

struct GPUTexture
{
    D3D12Resource resource;
    DescriptorAllocation srv;
    UINT width = 0;
    UINT height = 0;
};

class TextureLoader
{
public:
    void Initialize(ID3D12Device* device, ShaderVisibleDescriptorHeap* heap, D3D12CommandQueue* queue, D3D12CommandList* cmdList, UploadHeap* uploadHeap);
    GPUTexture LoadTexture2DFromFile(const std::wstring& path, UINT frameIndex = 0);
private:
    Microsoft::WRL::ComPtr<IWICImagingFactory> mWIC;
    ID3D12Device* mDevice = nullptr;
    ShaderVisibleDescriptorHeap* mHeap = nullptr;
    D3D12CommandQueue* mQueue = nullptr;
    D3D12CommandList* mCmdList = nullptr;
    UploadHeap* mUploadHeap = nullptr; // shared staging heap
};
