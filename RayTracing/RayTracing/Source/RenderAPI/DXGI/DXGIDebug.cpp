#include "pch.h"
#include "DXGIDebug.h"
#include "helpers.h"

DXGIDebug DXGIDebug::mInstance;

bool DXGIDebug::EnsureInitialized()
{
	return mDebug || Initialize();
}

bool DXGIDebug::Initialize()
{
	HRESULT hr = DXGIGetDebugInterface1(0, IID_PPV_ARGS(mDebug.ReleaseAndGetAddressOf()));
	RETURN_FAIL_HR(hr, "DXGIGetDebugInterface1 failed.");
	return true;
}

void DXGIDebug::Enable()
{
	if (!EnsureInitialized())
		return;

	mDebug->EnableLeakTrackingForThread();
}

void DXGIDebug::ReportLiveObjects()
{
	if (!EnsureInitialized())
		return;

	HRESULT hr = mDebug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);
	CHECK_HR(hr, "Failed to report live objects.");
}

