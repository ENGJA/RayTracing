#include "pch.h"

#include "Renderer.h"
#include "DXGI/DXGIFactory.h"
#include "helpers.h"


using std::wcout, std::endl;


void Renderer::Initialize(HWND hwnd)
{
	DXGIFactory factory;
	DXGIAdapter adapter = factory.GetAdapter();

	DXGI_ADAPTER_DESC desc;
	HRESULT hr = adapter->GetDesc(&desc);
	ASSERT_HR(hr, "Failed to get adapter description.");

	wcout << "Selected device: " << desc.Description << endl;

	mDevice.Initialize(adapter.Get());
}
