#include "pch.h"
#include "TextureLoader.h"
#include "helpers.h"

using Microsoft::WRL::ComPtr;

void TextureLoader::Initialize(ID3D12Device* pDevice, ShaderVisibleDescriptorHeap* heap, D3D12CommandQueue* queue, D3D12CommandList* cmdList, UploadHeap* uploadHeap, HLSLShader mipmapComputeShader)
{
	mDevice = pDevice;
	mHeap = heap;
	mQueue = queue;
	mCmdList = cmdList;
	mUploadHeap = uploadHeap;
	mMipmapGenerator.Initialize(pDevice, std::move(mipmapComputeShader), cmdList, queue);
	HRESULT hr = CoCreateInstance(
		CLSID_WICImagingFactory,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(mWIC.ReleaseAndGetAddressOf()));
	ASSERT_HR(hr, L"Failed to create WIC factory");
}

static UINT CalculateMipLevels(UINT width, UINT height)
{
	//return 1;
	UINT levels = 1;
	while (width > 1 || height > 1)
	{
		width = std::max(1u, width / 2);
		height = std::max(1u, height / 2);
		levels++;
	}
	return levels;
}

DecodedImage TextureLoader::DecodeImageRGBA8(const std::wstring& path)
{
	DecodedImage img{};

	ComPtr<IWICBitmapDecoder> decoder;
	HRESULT hr = mWIC->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.ReleaseAndGetAddressOf());
	ASSERT_HR(hr, L"Failed to open image file");

	ComPtr<IWICBitmapFrameDecode> frame;
	hr = decoder->GetFrame(0, frame.ReleaseAndGetAddressOf());
	ASSERT_HR(hr, L"Failed to decode image frame");

	UINT width = 0, height = 0;
	hr = frame->GetSize(&width, &height);
	ASSERT_HR(hr, L"Failed to get image size");
	img.width = width; img.height = height;

	WICPixelFormatGUID srcFormat{};
	hr = frame->GetPixelFormat(&srcFormat);
	ASSERT_HR(hr, L"Failed to get pixel format");

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
	img.pixels.resize(static_cast<size_t>(rowPitch) * height);

	WICRect rect{ 0, 0, static_cast<INT>(width), static_cast<INT>(height) };
	if (needsConvert)
		hr = converter->CopyPixels(&rect, rowPitch, static_cast<UINT>(img.pixels.size()), img.pixels.data());
	else
		hr = frame->CopyPixels(&rect, rowPitch, static_cast<UINT>(img.pixels.size()), img.pixels.data());
	ASSERT_HR(hr, L"Failed to copy pixels");

	return img;
}


GPUTexture TextureLoader::CreateTextureFromDecodedImage(const DecodedImage& img, UINT frameIndex, const std::function<void()>& executeQueue)
{
	// Describe and create the texture resource
	const UINT mipLevels = CalculateMipLevels(img.width, img.height);
	D3D12_RESOURCE_DESC desc = CreateTexture2DDesc(img.width, img.height, mipLevels);
	GPUTexture gpuTex{};
	gpuTex.width = img.width;
	gpuTex.height = img.height;
	gpuTex.format = desc.Format;
	gpuTex.mipLevels = mipLevels;
	gpuTex.resource.Initialize(mDevice, desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);

	// Define the layout of the subresource data
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
	UINT numRows = 0;
	UINT64 rowSize = 0;
	UINT64 totalBytes = 0;
	mDevice->GetCopyableFootprints(&desc, 0, 1, 0, &footprint, &numRows, &rowSize, &totalBytes);


	// Allocate upload heap space and copy data
	if (!mUploadHeap->CanAllocate(totalBytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT))
		executeQueue();

	auto alloc = mUploadHeap->Allocate(totalBytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);
	if (!alloc.cpuPtr) throw std::runtime_error("Upload heap out of space for texture.");

	const UINT srcRowPitch = img.width * 4; // RGBA8
	BYTE* dst = reinterpret_cast<BYTE*>(alloc.cpuPtr);
	footprint.Offset = alloc.offset;

	for (UINT y = 0; y < img.height; ++y)
	{
		memcpy(dst + y * footprint.Footprint.RowPitch, img.pixels.data() + y * srcRowPitch, srcRowPitch);
	}

	D3D12_TEXTURE_COPY_LOCATION dstLoc{};
	dstLoc.pResource = gpuTex.resource.Get();
	dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dstLoc.SubresourceIndex = 0;

	D3D12_TEXTURE_COPY_LOCATION srcLoc{};
	srcLoc.pResource = mUploadHeap->GetResource();
	srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	srcLoc.PlacedFootprint = footprint;

	mCmdList->Get()->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);

	D3D12_RESOURCE_BARRIER barrier = CreateTextureTransitionBarrier(gpuTex.resource.Get());
	mCmdList->Get()->ResourceBarrier(1, &barrier);

	mMipmapGenerator.GenerateMipmaps(gpuTex.resource.Get(), img.width, img.height, mipLevels, desc.Format, frameIndex, executeQueue);

	return gpuTex;
}

D3D12_RESOURCE_DESC TextureLoader::CreateTexture2DDesc(UINT width, UINT height, UINT16 mipLevels)
{
	return
	{
		.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D,
		.Width = width,
		.Height = height,
		.DepthOrArraySize = 1,
		.MipLevels = mipLevels,
		.Format = DXGI_FORMAT_R8G8B8A8_UNORM,
		.SampleDesc = { 1, 0 },
		.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN,
		.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
	};
}

D3D12_RESOURCE_BARRIER TextureLoader::CreateTextureTransitionBarrier(ID3D12Resource* pResource)
{
	return
	{
		.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION,
		.Transition = 
			{
			.pResource = pResource,
			.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,
			.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST,
			//.StateAfter = D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			}
	};
}



GPUTexture TextureLoader::LoadTexture2DFromFile(const std::wstring& path, UINT frameIndex, const std::function<void()>& executeQueue)
{
	DecodedImage img = DecodeImageRGBA8(path);
	return CreateTextureFromDecodedImage(img, frameIndex, executeQueue);
}

void TextureLoader::EnsureFallbackTexture(UINT frameIndex, const std::function<void()>& executeQueue)
{
	// If already created, do nothing
	if (mFallbackTexture.resource.Get() != nullptr)
		return;

	DecodedImage img{};
	img.width = 1;
	img.height = 1;
	img.pixels.resize(4);
	// Black pixel, opaque
	img.pixels[0] = 0;   // R
	img.pixels[1] = 0;   // G
	img.pixels[2] = 0;   // B
	img.pixels[3] = 255; // A

	// Create fallback GPU texture using the same upload / mip generation path
	mFallbackTexture = CreateTextureFromDecodedImage(img, frameIndex, executeQueue);
}
