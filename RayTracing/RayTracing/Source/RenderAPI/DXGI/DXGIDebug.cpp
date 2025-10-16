#include "pch.h"
#include "DXGIDebug.h"
#include "helpers.h"

DXGIDebug DXGIDebug::mInstance;

bool DXGIDebug::EnsureInitialized()
{
	return Get() || Initialize();
}

bool DXGIDebug::Initialize()
{
	HRESULT hr = DXGIGetDebugInterface1(0, IID_PPV_ARGS(&ptr_));
	RETURN_FAIL_HR(hr, "DXGIGetDebugInterface1 failed.");
	return true;
}

void DXGIDebug::Enable()
{
	if(!EnsureInitialized())
		return;

	Get()->EnableLeakTrackingForThread();
}

void DXGIDebug::ReportLiveObjects()
{
	if (!EnsureInitialized())
		return;

	HRESULT hr = Get()->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
	CHECK_HR(hr, "Failed to report live objects.");
}

