#include "pch.h"
#include "Application.h"
#include "RenderAPI/DXGI/DXGIDebug.h"
#include "Input/InputManager.h"

// ImGui includes for Win32 message handler
#include "imgui.h"
#include "imgui_impl_win32.h"

// ImGui forward declaration for Win32 message handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

using std::cout, std::cerr, std::endl;

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	// Let ImGui handle the message first
	if (ImGui_ImplWin32_WndProcHandler(hwnd, uMsg, wParam, lParam))
		return true;

	InputManager::Instance.OnWindowMessage(uMsg, wParam, lParam);

	Application* app = reinterpret_cast<Application*>(GetWindowLongPtr(hwnd, GWLP_USERDATA));

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
	case WM_SIZE:
	{
		if (app)
		{
			int width = LOWORD(lParam);
			int height = HIWORD(lParam);
			app->OnResize(width, height);
		}
		break;
	}
	case WM_LBUTTONDOWN:
	{
		// ImGui handles clicks when menu is open via WndProcHandler
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
	mWidth = width;
	mHeight = height;

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

	mIsRunning = true;
	return true;
}

void Application::Update()
{
	auto& input = InputManager::Instance;
	InputManager::Instance.BeginFrame();

	MSG msg;
	while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	double dt = static_cast<double>(now.QuadPart - mPrevCounter.QuadPart) * mSecondsPerCount;
	mPrevCounter = now;

	input.ProcessCallbacks(static_cast<float>(dt));

	// Always begin ImGui frame (required for WndProcHandler to work)
	mRenderer.BeginImGuiFrame();

	// Render UI through UIManager
	mUIManager.RenderUI(mCurrentState);

	// Update camera only in Scene mode
	if (mCurrentState == UIManager::AppState::Scene)
	{
		mCameraManager.Update(static_cast<float>(dt));
	}

	// Render the scene (if loaded)
	DirectX::XMMATRIX vp = mCameraManager.GetActiveViewProjection();
	DirectX::XMFLOAT3 camPos = mCameraManager.GetActiveCameraPosition();
	DirectX::XMFLOAT3 camForward = mCameraManager.GetActiveCameraForward();

	mRenderer.Update(vp, camPos, camForward);
}

void Application::OnCreate(HWND hwnd)
{
	cout << "Application OnCreate called!" << endl;
	auto& input = InputManager::Instance;
	input.Initialize(hwnd);

	// Initialize UIManager with dependencies
	mUIManager.Initialize(hwnd, &mCameraManager);
	
	// Set UI callbacks
	mUIManager.SetLoadSceneCallback([this](const std::string& path) {
		LoadScene(path);
	});
	
	mUIManager.SetUnloadSceneCallback([this]() {
		UnloadScene();
	});
	
	mUIManager.SetToggleMenuCallback([this]() {
		ToggleMenu();
	});
	
	mUIManager.SetExitCallback([this]() {
		ExitApplication();
	});
	
	mUIManager.SetExitToMainMenuCallback([this]() {
		ExitToMainMenu();
	});
	
	// Input callbacks
	input.RegisterKeyPressedCallback(VK_ESCAPE, [this]() {
		if (mCurrentState == UIManager::AppState::Scene)
			this->ToggleMenu();
	});
	
	// Toggle settings window with F1
	input.RegisterKeyPressedCallback(VK_F1, [this]() {
		if (mCurrentState == UIManager::AppState::Scene)
			mUIManager.ToggleSettings();
	});

	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	mSecondsPerCount = 1.0 / static_cast<double>(freq.QuadPart);
	QueryPerformanceCounter(&mPrevCounter);

	mCameraManager.Initialize(mWidth, mHeight);
	mRenderer.Initialize(hwnd, mWidth, mHeight);
	
	// Start with cursor visible in loading menu
input.SetCursorLocked(false);
	mCameraManager.SetActive(false);
}

void Application::OnDestroy()
{
	cout << "Application OnDestroy called!" << endl;
	mIsRunning = false;
}

void Application::OnResize(int width, int height)
{
	mWidth = width;
	mHeight = height;
}

void Application::ToggleMenu()
{
	auto& input = InputManager::Instance;
	if (mCurrentState == UIManager::AppState::Scene)
	{
		mCurrentState = UIManager::AppState::Menu;
		cout << "Switching to MENU mode." << endl;
		input.SetCursorLocked(false);
		mCameraManager.SetActive(false);
	}
	else if (mCurrentState == UIManager::AppState::Menu)
	{
		mCurrentState = UIManager::AppState::Scene;
		cout << "Switching to SCENE mode." << endl;
		input.SetCursorLocked(true);
		mCameraManager.SetActive(true);
	}
}

void Application::LoadScene(const std::string& path)
{
	mCurrentScenePath = path;
	mSceneLoaded = mRenderer.LoadScene(path);
	
	if (mSceneLoaded)
	{
		// Switch to scene mode after successful load
		mCurrentState = UIManager::AppState::Scene;
		InputManager::Instance.SetCursorLocked(true);
		mCameraManager.SetActive(true);
	}
}

void Application::UnloadScene()
{
	mRenderer.UnloadScene();
	mSceneLoaded = false;
	mCurrentScenePath.clear();
}

void Application::ExitToMainMenu()
{
	UnloadScene();
	mCurrentState = UIManager::AppState::LoadingMenu;
	InputManager::Instance.SetCursorLocked(false);
	mCameraManager.SetActive(false);
}

void Application::ExitApplication()
{
	mIsRunning = false;
	PostQuitMessage(0);
}
