#pragma once
#include "D3D12/D3D12Device.h"

class Renderer
{
private:
	D3D12Device mDevice;

public:
	~Renderer();
	void Initialize(HWND hwnd);
	void Release();
};

