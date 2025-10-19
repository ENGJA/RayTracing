#pragma once

#include "D3D12/Command/D3D12CommandList.h"
#include "D3D12/Command/D3D12CommandQueue.h"
#include "D3D12/D3D12Device.h"
#include "DXGI/DXGISwapChain.h"

class Renderer
{
private:
	D3D12Device mDevice;
	DXGISwapChain mSwapChain;
	D3D12CommandQueue mCommandQueue;
	D3D12CommandList mCommandList;

	UINT mWidth = 0;
	UINT mHeight = 0;

public:
	void Initialize(HWND hwnd, UINT width, UINT height);
	void Update();
};

