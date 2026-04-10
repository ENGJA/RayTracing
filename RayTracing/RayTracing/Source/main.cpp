#include "pch.h"
#include "Application/Application.h"
#include <RenderAPI/DXGI/DXGIDebug.h>

using std::cerr, std::endl;

// Export D3D12 Agility SDK version for Windows 10 compatibility
extern "C" { __declspec(dllexport) extern const UINT D3D12SDKVersion = 618; }
extern "C" { __declspec(dllexport) extern const char* D3D12SDKPath = ".\\D3D12\\"; }

int main()
{
	LPCWSTR className = L"RayTracingWindowClass";
	LPCWSTR windowName = L"Ray Tracing Application";

	HRESULT comHr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (comHr == RPC_E_CHANGED_MODE)
		comHr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(comHr)) return -1;

	{
		Application app;

		// Initialize the application
		if (!app.Initialize(className, windowName))
		{
			cerr << "Failed to initialize application." << endl;
			return -1;
		}

		// Main loop
		while (app.IsRunning())
		{
			app.Update();
		}
	}

	CoUninitialize();
#ifdef _DEBUG
	DXGIDebug::GetInstance().ReportLiveObjects();
#endif

	return 0;
}