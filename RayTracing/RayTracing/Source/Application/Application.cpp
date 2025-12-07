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

// Enable DPI awareness for proper scaling
static void EnableDPIAwareness()
{
	// Try modern DPI awareness API first (Windows 10 1703+)
	typedef BOOL(WINAPI* SetProcessDpiAwarenessContextFunc)(DPI_AWARENESS_CONTEXT);
	HMODULE user32 = LoadLibraryW(L"user32.dll");
	if (user32)
	{
		auto SetProcessDpiAwarenessContextPtr = (SetProcessDpiAwarenessContextFunc)GetProcAddress(user32, "SetProcessDpiAwarenessContext");
		if (SetProcessDpiAwarenessContextPtr)
		{
			SetProcessDpiAwarenessContextPtr(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
			FreeLibrary(user32);
			return;
		}
		FreeLibrary(user32);
	}

	// Fallback to older API (Windows 8.1+)
	typedef HRESULT(WINAPI* SetProcessDpiAwarenessFunc)(int);
	HMODULE shcore = LoadLibraryW(L"Shcore.dll");
	if (shcore)
	{
		auto SetProcessDpiAwarenessPtr = (SetProcessDpiAwarenessFunc)GetProcAddress(shcore, "SetProcessDpiAwareness");
		if (SetProcessDpiAwarenessPtr)
		{
			SetProcessDpiAwarenessPtr(2); // PROCESS_PER_MONITOR_DPI_AWARE
			FreeLibrary(shcore);
			return;
		}
		FreeLibrary(shcore);
	}

	// Last resort: Windows Vista+ API
	SetProcessDPIAware();
}

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
	wc.style = CS_HREDRAW | CS_VREDRAW; // Redraw on resize
	return RegisterClass(&wc);
}

bool Application::Initialize(LPCWSTR className, LPCWSTR windowName, int width, int height)
{
	// Enable DPI awareness BEFORE creating the window
	EnableDPIAwareness();

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

	// Update performance monitor
	mPerformanceMonitor.Update(static_cast<float>(dt));

	input.ProcessCallbacks(static_cast<float>(dt));

	// Always begin ImGui frame (required for WndProcHandler to work)
	mRenderer.BeginImGuiFrame();

	// Render UI through UIManager
	mUIManager.RenderUI(mCurrentState);

	// Update camera and cursor lock state only in Scene mode
	if (mCurrentState == UIManager::AppState::Scene)
	{
		// Unlock cursor if any UI window is open, lock it otherwise
		bool shouldLockCursor = !mUIManager.IsAnyWindowOpen();
		input.SetCursorLocked(shouldLockCursor);
		mCameraManager.SetActive(shouldLockCursor);
		
		if (shouldLockCursor)
		{
			mCameraManager.Update(static_cast<float>(dt));
		}
	}

	// Render the scene (if loaded)
	DirectX::XMMATRIX vp = mCameraManager.GetActiveViewProjection();
	DirectX::XMFLOAT3 camPos = mCameraManager.GetActiveCameraPosition();
	DirectX::XMFLOAT3 camForward = mCameraManager.GetActiveCameraForward();

	mRenderer.Update(vp, camPos, camForward);

	// Process scene loading AFTER rendering current frame
	// This ensures the loading screen is visible before we start loading
	if (mCurrentState == UIManager::AppState::LoadingScene)
	{
		ProcessSceneLoading();
	}
}

void Application::OnCreate(HWND hwnd)
{
	cout << "Application OnCreate called!" << endl;
	auto& input = InputManager::Instance;
	input.Initialize(hwnd);

	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	mSecondsPerCount = 1.0 / static_cast<double>(freq.QuadPart);
	QueryPerformanceCounter(&mPrevCounter);

	mCameraManager.Initialize(mWidth, mHeight);
	mRenderer.Initialize(hwnd, mWidth, mHeight);
	
	// Initialize performance monitor with adapter from renderer
	if (!mPerformanceMonitor.Initialize(mRenderer.GetAdapter()))
	{
		cerr << "Warning: Failed to initialize performance monitor. Some metrics may be unavailable." << endl;
	}

	// Initialize UIManager with dependencies
	mUIManager.Initialize(hwnd, &mCameraManager, &mPerformanceMonitor);
	
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

	// Toggle performance overlay with F2
	input.RegisterKeyPressedCallback(VK_F2, [this]() {
		if (mCurrentState == UIManager::AppState::Scene)
			mUIManager.TogglePerformanceOverlay();
	});
	
	// Start with cursor visible in loading menu
	input.SetCursorLocked(false);
	mCameraManager.SetActive(false);
}

void Application::OnDestroy()
{
	cout << "Application OnDestroy called!" << endl;
	mPerformanceMonitor.Shutdown();
	mIsRunning = false;
}

void Application::OnResize(int width, int height)
{
	if (width <= 0 || height <= 0)
		return; // Ignore invalid sizes

	mWidth = width;
	mHeight = height;

	// Update renderer resources (swap chain, depth buffer, viewport)
	mRenderer.OnResize(static_cast<UINT>(width), static_cast<UINT>(height));

	// Update camera aspect ratio
	mCameraManager.OnResize(static_cast<UINT>(width), static_cast<UINT>(height));
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
	// Extract filename from path for display
	std::filesystem::path fsPath(path);
	std::string filename = fsPath.filename().string();
	
	mUIManager.SetLoadingSceneName(filename);
	mPendingSceneLoad = path;
	mLoadingFrameCount = 0;
	
	// Switch to loading state
	mCurrentState = UIManager::AppState::LoadingScene;
	InputManager::Instance.SetCursorLocked(false);
	mCameraManager.SetActive(false);
}

void Application::ProcessSceneLoading()
{
	if (mPendingSceneLoad.empty())
		return;

	mLoadingFrameCount++;

	// First frame: just show loading screen, don't start loading yet
	// This ensures the loading UI is presented to the user
	if (mLoadingFrameCount == 1)
	{
		// Do nothing, just let the frame render
		return;
	}

	// Second frame: actually load the scene
	if (mLoadingFrameCount == 2)
	{
		// Actually load the scene
		mCurrentScenePath = mPendingSceneLoad;
		mPendingSceneLoad.clear();
		mLoadingFrameCount = 0;
		
		cout << "Loading scene: " << mCurrentScenePath << endl;
		mSceneLoaded = mRenderer.LoadScene(mCurrentScenePath);
		
		if (mSceneLoaded)
		{
			// Switch to scene mode after successful load
			mCurrentState = UIManager::AppState::Scene;
			InputManager::Instance.SetCursorLocked(true);
			mCameraManager.SetActive(true);
			cout << "Scene loaded successfully!" << endl;
		}
		else
		{
			// Loading failed, return to menu
			cerr << "Failed to load scene: " << mCurrentScenePath << endl;
			mCurrentState = UIManager::AppState::LoadingMenu;
		}
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
