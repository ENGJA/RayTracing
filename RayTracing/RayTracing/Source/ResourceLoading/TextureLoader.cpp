#include "pch.h"
#include "TextureLoader.h"
#include "helpers.h"

using Microsoft::WRL::ComPtr;

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


GPUTexture TextureLoader::CreateTextureFromDecodedImage(const DecodedImage& img, const std::function<void()>& executeQueue)
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

	mMipmapGenerator.GenerateMipmaps(gpuTex.resource.Get(), img.width, img.height, mipLevels, desc.Format, executeQueue);

	return gpuTex;
}

GPUTexture TextureLoader::CreateSolidDummyTexture(uint32_t color)
{
	D3D12_RESOURCE_DESC desc = CreateTexture2DDesc(1, 1, 1);

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

	D3D12_RESOURCE_BARRIER barrier = CreateTextureTransitionBarrier(gpuTex.resource.Get());
	barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
	mCmdList->Get()->ResourceBarrier(1, &barrier);

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
			.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			}
	};
}