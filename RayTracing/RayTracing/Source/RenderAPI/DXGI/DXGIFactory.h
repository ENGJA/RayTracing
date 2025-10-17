#pragma once
#include "DXGIAdapter.h"

class DXGIFactory //: Microsoft::WRL::ComPtr<IDXGIFactory7>
{
private:
	Microsoft::WRL::ComPtr<IDXGIFactory7> mFactory;
public:
	DXGIFactory();
	DXGIAdapter GetAdapter();
};

