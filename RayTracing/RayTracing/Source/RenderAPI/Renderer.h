#pragma once

#include "D3D12/Command/D3D12CommandList.h"
#include "D3D12/Command/D3D12CommandQueue.h"
#include "D3D12/D3D12Device.h"
#include "D3D12/D3D12PipelineState.h"
#include "D3D12/D3D12Resource.h"
#include "DataTypes.h"
#include "Depth/DepthBuffer.h"
#include "DXGI/DXGISwapChain.h"

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
	// temporary end

	DepthBuffer mDepthBuffer;

	D3D12_VIEWPORT mViewport{};
	D3D12_RECT mScissorRect{};

	ConstantBufferData mConstantBufferData{};
	D3D12Resource mConstantBuffer;

	D3D12CommandQueue mCommandQueue;
public:
	void Initialize(HWND hwnd, UINT width, UINT height);
	void Update();
};

