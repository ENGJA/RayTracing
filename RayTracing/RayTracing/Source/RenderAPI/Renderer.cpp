#include "pch.h"

#include "Renderer.h"
#include "DXGI/DXGIFactory.h"
#include "DXGI/DXGIDebug.h"
#include "D3D12/D3D12Debug.h"
#include "helpers.h"


using std::wcout, std::endl;


Renderer::~Renderer()
{
	Release();
}

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
}

void Renderer::Release()
{
	mDevice.Reset();
}

