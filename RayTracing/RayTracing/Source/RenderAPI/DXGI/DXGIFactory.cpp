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

	HRESULT hr = CreateDXGIFactory2(flags, IID_PPV_ARGS(&ptr_));
	ASSERT_HR(hr, "CreateDXGIFactory2 failed.");
}

DXGIAdapter DXGIFactory::GetAdapter()
{
	if (!Get())
	{
		cerr << "DXGIFactory is not initialized." << endl;
		throw;
	}

	DXGIAdapter adapter;
	HRESULT hr = Get()->EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter));
	ASSERT_HR(hr, "EnumAdapterByGpuPreference failed.");

	return adapter;
}
