#pragma once
#include "D3D12Device.h"

class Renderer
{
private:
	D3D12Device mDevice;

public:
	void Initialize(HWND hwnd);
};

