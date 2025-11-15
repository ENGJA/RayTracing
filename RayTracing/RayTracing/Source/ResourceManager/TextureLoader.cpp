#include "pch.h"
#include "TextureLoader.h"
#include "helpers.h"

using Microsoft::WRL::ComPtr;

void TextureLoader::Initialize(ID3D12Device* device, ShaderVisibleDescriptorHeap* heap)
{
    mDevice = device;
    mHeap = heap;
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

GPUTexture TextureLoader::LoadTexture2DFromFile(const std::wstring& path)
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

    // Read into CPU buffer
    const UINT bpp = 4; // RGBA8
    const UINT rowPitch = width * bpp;
    std::vector<BYTE> pixels(size_t(rowPitch) * height);

    WICRect rect{ 0, 0, int(width), int(height) };
    if (needsConvert)
        hr = converter->CopyPixels(&rect, rowPitch, (UINT)pixels.size(), pixels.data());
    else
        hr = frame->CopyPixels(&rect, rowPitch, (UINT)pixels.size(), pixels.data());
    ASSERT_HR(hr, L"Failed to copy pixels");

    // Create GPU resource in DEFAULT heap
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

    // Upload heap and copy
    UINT64 uploadSize = 0;
    mDevice->GetCopyableFootprints(&desc, 0, 1, 0, nullptr, nullptr, nullptr, &uploadSize);

    D3D12Resource upload;
    upload.Initialize(mDevice, (unsigned int)uploadSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);

    D3D12_SUBRESOURCE_DATA sub{};
    sub.pData = pixels.data();
    sub.RowPitch = rowPitch;
    sub.SlicePitch = size_t(rowPitch) * height;

    // Create a temporary command list to upload
    ComPtr<ID3D12CommandAllocator> alloc;
    mDevice->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(alloc.ReleaseAndGetAddressOf()));
    ComPtr<ID3D12GraphicsCommandList> cmd;
    mDevice->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(cmd.ReleaseAndGetAddressOf()));

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
    UINT numRows = 0; UINT64 rowSize = 0; UINT64 total = 0;
    mDevice->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &numRows, &rowSize, &total);

    BYTE* mapped = nullptr;
    D3D12_RANGE range{ 0, 0 };
    upload.Get()->Map(0, &range, reinterpret_cast<void**>(&mapped));
    for (UINT y = 0; y < height; ++y)
    {
        memcpy(mapped + y * footprint.Footprint.RowPitch, pixels.data() + y * rowPitch, rowPitch);
    }
    upload.Get()->Unmap(0, nullptr);

    D3D12_TEXTURE_COPY_LOCATION dst{};
    dst.pResource = gpuTex.resource.Get();
    dst.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
    dst.SubresourceIndex = 0;

    D3D12_TEXTURE_COPY_LOCATION src{};
    src.pResource = upload.Get();
    src.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
    src.PlacedFootprint = footprint;

    cmd->CopyTextureRegion(&dst, 0, 0, 0, &src, nullptr);

    // Transition to PIXEL_SHADER_RESOURCE
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition.pResource = gpuTex.resource.Get();
    barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    cmd->ResourceBarrier(1, &barrier);

    cmd->Close();

    // Execute and wait using a transient queue
    ComPtr<ID3D12CommandQueue> queue;
    D3D12_COMMAND_QUEUE_DESC qd{}; qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    mDevice->CreateCommandQueue(&qd, IID_PPV_ARGS(queue.ReleaseAndGetAddressOf()));
    ID3D12CommandList* lists[] = { cmd.Get() };
    queue->ExecuteCommandLists(1, lists);

    ComPtr<ID3D12Fence> fence; UINT64 fv = 1;
    mDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(fence.ReleaseAndGetAddressOf()));
    HANDLE e = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    queue->Signal(fence.Get(), fv);
    if (fence->GetCompletedValue() < fv)
    {
        fence->SetEventOnCompletion(fv, e);
        WaitForSingleObject(e, INFINITE);
    }
    CloseHandle(e);

    // Create SRV in shader visible heap
    CreateSRV(mDevice, gpuTex.resource.Get(), desc.Format, mHeap, gpuTex.srv);
    return gpuTex;
}
