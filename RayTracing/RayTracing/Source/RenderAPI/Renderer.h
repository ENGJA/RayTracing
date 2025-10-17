#pragma once

#include "D3D12/Command/D3D12CommandList.h"
#include "D3D12/Command/D3D12CommandQueue.h"
#include "D3D12/D3D12Device.h"

class Renderer
{
private:
	D3D12Device mDevice;
	D3D12CommandQueue mCommandQueue;
	D3D12CommandList mCommandList;

public:
	~Renderer();
	void Initialize(HWND hwnd);
	void Release();
};

