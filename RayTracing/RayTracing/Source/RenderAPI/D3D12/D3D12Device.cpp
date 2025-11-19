#include "pch.h"
#include "D3D12Device.h"
#include "helpers.h"


void D3D12Device::Initialize(IDXGIAdapter* pAdapter)
{
	HRESULT hr = D3D12CreateDevice(pAdapter, D3D_FEATURE_LEVEL_12_1, IID_PPV_ARGS(mDevice.ReleaseAndGetAddressOf()));
	ASSERT_HR(hr, "Failed to create D3D12 device");
}
