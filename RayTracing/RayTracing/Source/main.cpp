#include "pch.h"
#include "Application/Application.h"
#include <RenderAPI/DXGI/DXGIDebug.h>

using std::cerr, std::endl;

int main()
{
	LPCWSTR className = L"RayTracingWindowClass";
	LPCWSTR windowName = L"Ray Tracing Application";
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

#ifdef _DEBUG
	DXGIDebug::GetInstance().ReportLiveObjects();
#endif

	return 0;
}