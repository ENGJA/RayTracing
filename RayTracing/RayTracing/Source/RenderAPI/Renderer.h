#pragma once

#include "D3D12/Command/D3D12CommandList.h"
#include "D3D12/Command/D3D12CommandQueue.h"
#include "D3D12/D3D12Device.h"
#include "D3D12/D3D12PipelineState.h"
#include "D3D12/D3D12Resource.h"
#include "DataTypes.h"
#include "Depth/DepthBuffer.h"
#include "DXGI/DXGISwapChain.h"

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

	// temporary
	D3D12Resource mVertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW mVertexBufferView{};

	D3D12Resource mIndexBuffer;
	D3D12_INDEX_BUFFER_VIEW mIndexBufferView{};
	// temporary end

	DepthBuffer mDepthBuffer;

	D3D12_VIEWPORT mViewport{};
	D3D12_RECT mScissorRect{};

	ConstantBufferData mConstantBufferData{};
	D3D12Resource mConstantBuffer;

	D3D12CommandQueue mCommandQueue;
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

