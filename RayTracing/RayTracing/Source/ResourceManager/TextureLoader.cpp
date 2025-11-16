#include "pch.h"
#include "TextureLoader.h"
#include "helpers.h"

using Microsoft::WRL::ComPtr;

void TextureLoader::Initialize(ID3D12Device* device, ShaderVisibleDescriptorHeap* heap, D3D12CommandQueue* queue, D3D12CommandList* cmdList, UploadHeap* uploadHeap)
{
    mDevice = device;
    mHeap = heap;
    mQueue = queue;
    mCmdList = cmdList;
    mUploadHeap = uploadHeap;
    HRESULT hr = CoCreateInstance(
        CLSID_WICImagingFactory,
        nullptr,
        CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(mWIC.ReleaseAndGetAddressOf()));
    ASSERT_HR(hr, L"Failed to create WIC factory");
}

static void CreateSRV(ID3D12Device* device, ID3D12Resource* res, DXGI_FORMAT format, ShaderVisibleDescriptorHeap* heap, DescriptorAllocation& out)
{
    out = heap->Allocate(1);
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{};
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srv.Format = format;
    srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Texture2D.MipLevels = 1;
    device->CreateShaderResourceView(res, &srv, out.cpuHandle);
}

GPUTexture TextureLoader::LoadTexture2DFromFile(const std::wstring& path, UINT frameIndex)
{
    GPUTexture gpuTex{};

    ComPtr<IWICBitmapDecoder> decoder;
    HRESULT hr = mWIC->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.ReleaseAndGetAddressOf());
    ASSERT_HR(hr, L"Failed to open image file");

    ComPtr<IWICBitmapFrameDecode> frame;
    hr = decoder->GetFrame(0, frame.ReleaseAndGetAddressOf());
    ASSERT_HR(hr, L"Failed to decode image frame");

    UINT width = 0, height = 0;
    frame->GetSize(&width, &height);
    gpuTex.width = width; gpuTex.height = height;

    WICPixelFormatGUID srcFormat{};
    frame->GetPixelFormat(&srcFormat);

    static WICPixelFormatGUID target = GUID_WICPixelFormat32bppRGBA;
    bool needsConvert = (srcFormat != target);

    ComPtr<IWICFormatConverter> converter;
    if (needsConvert)
    {
        hr = mWIC->CreateFormatConverter(converter.ReleaseAndGetAddressOf());
        ASSERT_HR(hr, L"Failed to create format converter");
        hr = converter->Initialize(frame.Get(), target, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
        ASSERT_HR(hr, L"Failed to init format converter");
    }

    const UINT bpp = 4; // RGBA8
    const UINT rowPitch = width * bpp;
    std::vector<BYTE> pixels(size_t(rowPitch) * height);

    WICRect rect{ 0, 0, int(width), int(height) };
    if (needsConvert)
        hr = converter->CopyPixels(&rect, rowPitch, (UINT)pixels.size(), pixels.data());
    else
        hr = frame->CopyPixels(&rect, rowPitch, (UINT)pixels.size(), pixels.data());
    ASSERT_HR(hr, L"Failed to copy pixels");

    D3D12_RESOURCE_DESC desc{};
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Width = width;
    desc.Height = height;
    desc.DepthOrArraySize = 1;
    desc.MipLevels = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc = { 1, 0 };
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.Flags = D3D12_RESOURCE_FLAG_NONE;

    gpuTex.resource.Initialize(mDevice, desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT numRows = 0; UINT64 rowSize = 0; UINT64 totalBytes = 0;
    mDevice->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &numRows, &rowSize, &totalBytes);

	mUploadHeap->Reset();
    auto alloc = mUploadHeap->Allocate(totalBytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
    if (!alloc.cpuPtr) throw std::runtime_error("Upload heap out of space for texture.");

    BYTE* dst = reinterpret_cast<BYTE*>(alloc.cpuPtr);
    footprint.Offset = alloc.offset;
    for (UINT y = 0; y < height; ++y)
    {
        memcpy(dst + y * footprint.Footprint.RowPitch, pixels.data() + y * rowPitch, rowPitch);
    }

    mCmdList->ResetCommandList(frameIndex);

    D3D12_TEXTURE_COPY_LOCATION dstLoc{};
    dstLoc.pResource = gpuTex.resource.Get();
    dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dstLoc.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION srcLoc{};
    srcLoc.pResource = mUploadHeap->GetResource();
    srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    srcLoc.PlacedFootprint = footprint;

    mCmdList->Get()->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = gpuTex.resource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    mCmdList->Get()->ResourceBarrier(1, &barrier);

    mCmdList->Get()->Close();
    ID3D12CommandList* lists[] = { mCmdList->Get() };
    mQueue->ExecuteCommandLists(1, lists);
    mQueue->Flush();

    CreateSRV(mDevice, gpuTex.resource.Get(), desc.Format, mHeap, gpuTex.srv);
    return gpuTex;
}
