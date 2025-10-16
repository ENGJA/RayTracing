#include "pch.h"
#include "D3D12Debug.h"
#include "helpers.h"

D3D12Debug D3D12Debug::mInstance;

bool D3D12Debug::EnsureInitialized()
{
	return Get() || Initialize();
}

bool D3D12Debug::Initialize()
{
	HRESULT hr = D3D12GetDebugInterface(IID_PPV_ARGS(&ptr_));
	RETURN_FAIL_HR(hr, "D3D12GetDebugInterface failed.");
	return true;
}

void D3D12Debug::Enable()
{
	if (!EnsureInitialized())
		return;

	Get()->EnableDebugLayer();
}
