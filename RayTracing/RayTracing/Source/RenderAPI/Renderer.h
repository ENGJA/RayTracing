#pragma once

#include "D3D12/Command/D3D12CommandList.h"
#include "D3D12/Command/D3D12CommandQueue.h"
#include "D3D12/D3D12Device.h"
#include "D3D12/D3D12PipelineState.h"
#include "D3D12/D3D12Resource.h"
#include "D3D12/UploadHeap.h"
#include "DataTypes.h"
#include "Depth/DepthBuffer.h"
#include "DXGI/DXGISwapChain.h"
#include "ResourceManager/Model.h"
#include "ResourceManager/TextureLoader.h"
#include "RenderAPI/Descriptors/ShaderVisibleDescriptorHeap.h"
#include <unordered_map>

struct MeshGpuData
{
	D3D12Resource vb;
	D3D12_VERTEX_BUFFER_VIEW vbv{};
	D3D12Resource ib;
	D3D12_INDEX_BUFFER_VIEW ibv{};
	DescriptorAllocation materialTable; // contiguous descriptors for material textures (t0-t4)
};

/**
 * @brief High level renderer that wires up D3D12 device, swap chain, pipeline, and per-frame resources.
 */
class Renderer
{
private:
	D3D12Device mDevice;
	DXGISwapChain mSwapChain;
	D3D12CommandList mCommandList;
	D3D12PipelineState mPipelineState;

	UINT mWidth = 0;
	UINT mHeight = 0;

	DepthBuffer mDepthBuffer;

	D3D12_VIEWPORT mViewport{};
	D3D12_RECT mScissorRect{};

	ConstantBufferData mConstantBufferData{};
	D3D12Resource mConstantBuffer;

	// GPU descriptors and textures
	ShaderVisibleDescriptorHeap mSrvHeap;
	TextureLoader mTextureLoader;
	UploadHeap mUploadHeap; // shared staging heap

	// Texture cache by path
	std::unordered_map<std::string, GPUTexture> mTextureCache;

	// Multiple models
	std::vector<std::unique_ptr<Model>> mModels;
	// Per-mesh GPU data packed after loading
	std::vector<MeshGpuData> mMeshGpu;

	// HAS to be last to ensure proper destruction order
	D3D12CommandQueue mCommandQueue;

	GPUTexture LoadOrGetTexture(const std::string& path);
	void BuildMeshGpuData();

public:
	/**
	 * @brief Creates device/swap chain and initializes resources.
	 * @param hwnd Window handle.
	 * @param width Client width.
	 * @param height Client height.
	 */
	void Initialize(HWND hwnd, UINT width, UINT height);
	/**
	 * @brief Records and submits commands for one frame and presents.
	 */
	void Update();
};

