#include "pch.h"
#include "TextureLoader.h"
#include "helpers.h"


using Microsoft::WRL::ComPtr;
using std::wcout, std::endl;

void TextureLoader::Initialize(ID3D12Device* pDevice, DescriptorHeap* heap, D3D12CommandQueue* queue, D3D12CommandList* cmdList, UploadHeap* uploadHeap, HLSLShader mipmapComputeShader)
{
	mDevice = pDevice;
	mHeap = heap;
	mQueue = queue;
	mCmdList = cmdList;
	mUploadHeap = uploadHeap;
	mMipmapGenerator.Initialize(pDevice, std::move(mipmapComputeShader), cmdList, queue);
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


GPUTexture TextureLoader::CreateTextureFromDecodedImage(const DecodedImage& img, const std::function<void()>& executeQueue, bool sRGB)
{
	// Describe and create the texture resource
	const UINT mipLevels = CalculateMipLevels(img.width, img.height);
	D3D12_RESOURCE_DESC desc = CreateTexture2DDesc(img.width, img.height, mipLevels, sRGB);
	
	// Calculate required memory
	UINT64 textureSize = 0;
	mDevice->GetCopyableFootprints(&desc, 0, 1, 0, nullptr, nullptr, nullptr, &textureSize);
	
	// Check if texture is small enough to use small resource placement
	if (textureSize < 65536)
	{
		desc.Alignment = D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;
	}
	
	GPUTexture gpuTex{};
	gpuTex.width = img.width;
	gpuTex.height = img.height;
	// For sRGB textures, store the SRGB format (not TYPELESS) for creating SRV later
	gpuTex.format = sRGB ? DXGI_FORMAT_R8G8B8A8_UNORM_SRGB : DXGI_FORMAT_R8G8B8A8_UNORM;
	gpuTex.mipLevels = mipLevels;
	
	try
	{
		gpuTex.resource.Initialize(mDevice, desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
	}
	catch (const std::exception& e)
	{
		// Texture creation failed - likely out of GPU memory
		wcout << L"Failed to create texture resource (" << img.width << L"x" << img.height 
		      << L", " << mipLevels << L" mips): " << e.what() << endl;
		throw std::runtime_error("GPU memory exhausted during texture creation");
	}

	// Define the layout of the subresource data
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};
	UINT numRows = 0;
	UINT64 rowSize = 0;
	UINT64 totalBytes = 0;
	
	// GetCopyableFootprints doesn't work with TYPELESS formats, use UNORM instead
	D3D12_RESOURCE_DESC descForFootprint = desc;
	if (desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS)
		descForFootprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	
	mDevice->GetCopyableFootprints(&descForFootprint, 0, 1, 0, &footprint, &numRows, &rowSize, &totalBytes);


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

	D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		gpuTex.resource.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	mCmdList->Get()->ResourceBarrier(1, &barrier);

	// For mipmap generation, use UNORM format (compute shaders can't write to sRGB or TYPELESS)
	DXGI_FORMAT mipmapFormat = (desc.Format == DXGI_FORMAT_R8G8B8A8_TYPELESS) 
		? DXGI_FORMAT_R8G8B8A8_UNORM 
		: desc.Format;
	
	mMipmapGenerator.GenerateMipmaps(gpuTex.resource.Get(), img.width, img.height, mipLevels, mipmapFormat, executeQueue);

	return gpuTex;
}

GPUTexture TextureLoader::CreateTextureFromDDSPath(const std::wstring& path, DirectX::ResourceUploadBatch& batch)
{
	GPUTexture gpuTex{};

	// 1. Create the Resource
	HRESULT hr = DirectX::CreateDDSTextureFromFile(
		mDevice,
		batch,
		path.c_str(),
		gpuTex.resource.GetAddressOf(), true);
	ASSERT_HR(hr, L"Failed to create texture from DDS file: " + path);

	D3D12_RESOURCE_DESC desc = gpuTex.resource.Get()->GetDesc();
	gpuTex.width = static_cast<UINT>(desc.Width);
	gpuTex.height = desc.Height;
	gpuTex.mipLevels = desc.MipLevels;
	gpuTex.format = desc.Format;
	return gpuTex;
}

GPUTexture TextureLoader::CreateSolidDummyTexture(uint32_t color)
{
	D3D12_RESOURCE_DESC desc = CreateTexture2DDesc(1, 1, 1, false); // Not sRGB for dummy textures

	desc.Alignment = D3D12_SMALL_RESOURCE_PLACEMENT_ALIGNMENT;

	GPUTexture gpuTex{};
	gpuTex.width = 1;
	gpuTex.height = 1;
	gpuTex.mipLevels = 1;
	gpuTex.format = desc.Format;
	gpuTex.resource.Initialize(mDevice, desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);

	const UINT rowPitch = 256; // D3D12_TEXTURE_DATA_PITCH_ALIGNMENT requirement
	const UINT totalBytes = rowPitch;

	auto alloc = mUploadHeap->Allocate(totalBytes, D3D12_TEXTURE_DATA_PLACEMENT_ALIGNMENT);

	uint8_t* pData = reinterpret_cast<uint8_t*>(alloc.cpuPtr);
	memcpy(pData, &color, 4); // Copy RGBA8 color

	D3D12_TEXTURE_COPY_LOCATION dstLoc{};
	dstLoc.pResource = gpuTex.resource.Get();
	dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
	dstLoc.SubresourceIndex = 0;

	D3D12_TEXTURE_COPY_LOCATION srcLoc{};
	srcLoc.pResource = mUploadHeap->GetResource();
	srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
	srcLoc.PlacedFootprint.Offset = alloc.offset;
	srcLoc.PlacedFootprint.Footprint.Format = desc.Format;
	srcLoc.PlacedFootprint.Footprint.Width = 1;
	srcLoc.PlacedFootprint.Footprint.Height = 1;
	srcLoc.PlacedFootprint.Footprint.Depth = 1;
	srcLoc.PlacedFootprint.Footprint.RowPitch = rowPitch;

	mCmdList->Get()->CopyTextureRegion(&dstLoc, 0, 0, 0, &srcLoc, nullptr);


	D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		gpuTex.resource.Get(),
		D3D12_RESOURCE_STATE_COPY_DEST,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
	mCmdList->Get()->ResourceBarrier(1, &barrier);

	return gpuTex;
}

D3D12_RESOURCE_DESC TextureLoader::CreateTexture2DDesc(UINT width, UINT height, UINT16 mipLevels, bool sRGB)
{
	// For sRGB textures, use TYPELESS format to allow both UNORM (UAV) and SRGB (SRV) views
	DXGI_FORMAT format = sRGB ? DXGI_FORMAT_R8G8B8A8_TYPELESS : DXGI_FORMAT_R8G8B8A8_UNORM;
	
	return CD3DX12_RESOURCE_DESC::Tex2D(
		format,
		width,
		height,
		1, // array size
		mipLevels,
		1, // sample count
		0, // sample quality
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
	);
}
