#pragma once
#include "DXGIAdapter.h"

class DXGIFactory : Microsoft::WRL::ComPtr<IDXGIFactory7>
{
public:
	DXGIFactory();
	DXGIAdapter GetAdapter();
};

