#include "pch.h"

#include "D3D12/D3D12Debug.h"
#include "DXGI/DXGIDebug.h"
#include "DXGI/DXGIFactory.h"
#include "helpers.h"
#include "Renderer.h"


using std::wcout, std::endl;

void Renderer::Initialize(HWND hwnd)
{
#ifdef _DEBUG
	D3D12Debug::GetInstance().Enable();
	DXGIDebug::GetInstance().Enable();
#endif

	DXGIFactory factory;
	DXGIAdapter adapter = factory.GetAdapter();

	DXGI_ADAPTER_DESC desc;
	HRESULT hr = adapter->GetDesc(&desc);
	ASSERT_HR(hr, "Failed to get adapter description.");

	wcout << "Selected device: " << desc.Description << endl;

	mDevice.Initialize(adapter.Get());
	mCommandQueue.Initialize(mDevice.Get());
	mCommandList.Initialize(mDevice.Get());
}

