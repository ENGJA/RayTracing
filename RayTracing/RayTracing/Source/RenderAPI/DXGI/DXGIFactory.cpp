#include "pch.h"
#include "DXGIFactory.h"
#include "helpers.h"

using std::cerr, std::endl;

DXGIFactory::DXGIFactory()
{
	UINT flags = 0;
#ifdef _DEBUG
	flags |= DXGI_CREATE_FACTORY_DEBUG;
#endif

	HRESULT hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(mFactory.ReleaseAndGetAddressOf()));
	ASSERT_HR(hr, "CreateDXGIFactory2 failed.");
}

DXGIAdapter DXGIFactory::GetAdapter()
{
	DXGIAdapter adapter;
	HRESULT hr = mFactory->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
	ASSERT_HR(hr, "EnumAdapterByGpuPreference failed.");

	return adapter;
}
