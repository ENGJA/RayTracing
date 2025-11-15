#pragma once
#include <string>
#include <vector>
#include "RenderAPI/D3D12/D3D12Resource.h"
#include "RenderAPI/Descriptors/ShaderVisibleDescriptorHeap.h"

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
    void Initialize(ID3D12Device* device, ShaderVisibleDescriptorHeap* heap);
    GPUTexture LoadTexture2DFromFile(const std::wstring& path);
private:
    Microsoft::WRL::ComPtr<IWICImagingFactory> mWIC;
    ID3D12Device* mDevice = nullptr;
    ShaderVisibleDescriptorHeap* mHeap = nullptr;
};
