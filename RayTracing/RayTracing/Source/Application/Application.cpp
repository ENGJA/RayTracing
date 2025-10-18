#include "pch.h"
#include "Application.h"
#include "RenderAPI/DXGI/DXGIDebug.h"

using std::cout, std::cerr, std::endl;

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_NCCREATE:
	{
		LPCREATESTRUCT pcs = reinterpret_cast<LPCREATESTRUCT>(lParam);
		SetWindowLongPtr(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pcs->lpCreateParams));

		cout << "Window non-client area created!" << endl;
		break;
	}
	case WM_CREATE:
	{
		Application* app = reinterpret_cast<Application*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
		app->OnCreate(hwnd);

		cout << "Window created!" << endl;
		break;
	}
	case WM_DESTROY:
	{
		Application* app = reinterpret_cast<Application*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
		app->OnDestroy();

		cout << "Window destroyed!" << endl;
		PostQuitMessage(0);
		break;
	}
	}

	return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

static ATOM RegisterWindowClass(HINSTANCE hInstance, LPCWSTR className)
{
	WNDCLASS wc{};
	wc.lpfnWndProc = WindowProc;
	wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
	wc.hInstance = hInstance;
	wc.lpszClassName = className;
	return RegisterClass(&wc);
}

bool Application::Initialize(LPCWSTR className, LPCWSTR windowName, int width, int height)
{
	if (!RegisterWindowClass(GetModuleHandle(NULL), className))
	{
		cerr << "Failed to register window class. Error: " << GetLastError() << endl;
		return false;
	}

	mHwnd = CreateWindow(className, windowName, WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT, width, height,
		NULL, NULL, GetModuleHandle(NULL), this);

	if (!mHwnd)
	{
		cerr << "Failed to create window. Error: " << GetLastError() << endl;
		return false;
	}

	ShowWindow(mHwnd, SW_SHOW);
	UpdateWindow(mHwnd);

	mWidth = width;
	mHeight = height;
	mIsRunning = true;
	return true;
}

void Application::Update()
{
	MSG msg;
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	mRenderer.Update();
}

void Application::OnCreate(HWND hwnd)
{
	cout << "Application OnCreate called!" << endl;
	mRenderer.Initialize(hwnd, mWidth, mHeight);
}

void Application::OnDestroy()
{
	cout << "Application OnDestroy called!" << endl;
	mIsRunning = false;
}




