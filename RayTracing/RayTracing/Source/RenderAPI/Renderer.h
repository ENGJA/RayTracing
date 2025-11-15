#pragma once

#include "D3D12/Command/D3D12CommandList.h"
#include "D3D12/Command/D3D12CommandQueue.h"
#include "D3D12/D3D12Device.h"
#include "D3D12/D3D12PipelineState.h"
#include "D3D12/D3D12Resource.h"
#include "DataTypes.h"
#include "Depth/DepthBuffer.h"
#include "DXGI/DXGISwapChain.h"
#include "ResourceManager/Model.h"
#include "ResourceManager/TextureLoader.h"
#include "RenderAPI/Descriptors/ShaderVisibleDescriptorHeap.h"

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

	D3D12CommandQueue mCommandQueue;

	// GPU descriptors and textures
	ShaderVisibleDescriptorHeap mSrvHeap;
	TextureLoader mTextureLoader;

	// A single model instance loaded from CPU-side Model class
	std::unique_ptr<Model> mModel;

	// GPU buffers for the current mesh (interleaved vertex: pos/normal/uv)
	D3D12Resource mVB;
	D3D12_VERTEX_BUFFER_VIEW mVBV{};
	D3D12Resource mIB;
	D3D12_INDEX_BUFFER_VIEW mIBV{};

	// Texture bound to t0
	GPUTexture mAlbedo;

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

