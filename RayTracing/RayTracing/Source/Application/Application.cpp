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
	//DirectX::XMMATRIX vp = mCameraManager.GetActiveViewProjection();
	//DirectX::XMFLOAT3 camPos = mCameraManager.GetActiveCameraPosition();
	//DirectX::XMFLOAT3 camForward = mCameraManager.GetActiveCameraForward();

	mRenderer.Update(mCameraManager.GetActiveCamera());

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
	
	mUIManager.SetLoadMultipleScenesCallback([this](const std::vector<std::string>& paths) {
		LoadMultipleScenes(paths);
	});
	
	mUIManager.SetAddExtensionScenesCallback([this](const std::vector<std::string>& paths) {
		AddExtensionScenes(paths);
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
	
	mUIManager.SetSetDLSSModeCallback([this](int mode) {
		mRenderer.SetDLSSMode(static_cast<sl::DLSSMode>(mode));
	});

	// Initialize UI with current DLSS mode
	mUIManager.SetCurrentDLSSMode(static_cast<int>(mRenderer.GetDLSSMode()));

	mUIManager.SetShadowsCallbacks(
		[this]() { return mRenderer.GetShadowsEnabled(); },
		[this](bool enabled) { mRenderer.SetShadowsEnabled(enabled); }
	);
	mUIManager.SetReflectionsCallbacks(
		[this]() { return mRenderer.GetReflectionsEnabled(); },
		[this](bool enabled) { mRenderer.SetReflectionsEnabled(enabled); }
	);
	mUIManager.SetMaxRecursionDepthCallbacks(
		[this]() { return mRenderer.GetMaxRecursionDepth(); },
		[this](int depth) { mRenderer.SetMaxRecursionDepth(depth); }
	);


	// Input callbacks
	input.RegisterKeyPressedCallback(VK_ESCAPE, [this]() {
		if (mCurrentState == UIManager::AppState::Scene || mCurrentState == UIManager::AppState::Menu)
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
	
	// Wait for loading thread to finish if still running
	if (mLoadingThread.joinable())
	{
		cout << "Waiting for loading thread to finish..." << endl;
		mLoadingThread.join();
	}
	
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
	std::vector<std::string> paths = { path };
	LoadMultipleScenes(paths);
}

void Application::LoadMultipleScenes(const std::vector<std::string>& paths)
{
	// If already loading, ignore
	if (mIsLoadingInProgress)
		return;

	if (paths.empty())
		return;

	// Display loading info
	std::string loadingText;
	if (paths.size() == 1)
	{
		std::filesystem::path fsPath(paths[0]);
		loadingText = fsPath.filename().string();
	}
	else
	{
		std::filesystem::path fsPath(paths[0]);
		loadingText = fsPath.filename().string();
		loadingText += "\n+ " + std::to_string(paths.size() - 1) + " additional scene(s)";
	}
	
	mUIManager.SetLoadingSceneName(loadingText);
	mPendingScenePaths = paths;
	mIsLoadingExtension = false;
	
	// Switch to loading state
	mCurrentState = UIManager::AppState::LoadingScene;
	InputManager::Instance.SetCursorLocked(false);
	mCameraManager.SetActive(false);
	
	// Reset loading flags
	mLoadingComplete = false;
	mLoadingSuccess = false;
	mIsLoadingInProgress = true;
	mPendingModel.reset();
	mPendingModels.clear();
	
	// Start async loading thread
	if (mLoadingThread.joinable())
		mLoadingThread.join();
	
	mLoadingThread = std::thread(&Application::PerformAsyncMultiLoad, this, paths);
}

void Application::AddExtensionScenes(const std::vector<std::string>& paths)
{
	// If already loading, ignore
	if (mIsLoadingInProgress)
		return;

	if (paths.empty())
		return;

	// Display loading info
	std::string loadingText;
	if (paths.size() == 1)
	{
		std::filesystem::path fsPath(paths[0]);
		loadingText = "Adding extension:\n" + fsPath.filename().string();
	}
	else
	{
		std::filesystem::path fsPath(paths[0]);
		loadingText = "Adding extensions:\n" + fsPath.filename().string();
		loadingText += "\n+ " + std::to_string(paths.size() - 1) + " more";
	}
	
	mUIManager.SetLoadingSceneName(loadingText);
	mPendingScenePaths = paths;
	mIsLoadingExtension = true;
	
	// Switch to loading state
	mCurrentState = UIManager::AppState::LoadingScene;
	InputManager::Instance.SetCursorLocked(false);
	mCameraManager.SetActive(false);
	
	// Reset loading flags
	mLoadingComplete = false;
	mLoadingSuccess = false;
	mIsLoadingInProgress = true;
	mPendingModel.reset();
	mPendingModels.clear();
	
	// Start async loading thread
	if (mLoadingThread.joinable())
		mLoadingThread.join();
	
	mLoadingThread = std::thread(&Application::PerformAsyncMultiLoad, this, paths);
}

void Application::PerformAsyncMultiLoad(const std::vector<std::string>& paths)
{
	cout << "Loading " << paths.size() << " scene(s) in background thread..." << endl;
	
	try
	{
		std::vector<std::unique_ptr<Model>> models;
		size_t successCount = 0;
		
		for (size_t i = 0; i < paths.size(); ++i)
		{
			const auto& path = paths[i];
			cout << "Loading [" << (i + 1) << "/" << paths.size() << "]: " << path << endl;
			
			try
			{
				auto model = std::make_unique<Model>();
				model->loadModel(path);
				
				if (!model->mMeshes.empty())
				{
					models.push_back(std::move(model));
					successCount++;
					cout << "  -> Success (" << models.back()->mMeshes.size() << " meshes)" << endl;
				}
				else
				{
					cout << "  -> Warning: Scene contains no meshes, creating fallback quad for testing." << endl;
					
					// Create fallback quad for testing
					std::vector<::Vertex> cpuVerts = {
						{ { -1, -1, 0 }, {0,0,1}, {0,1}, {1,0,0,1} },
						{ { -1,  1, 0 }, {0,0,1}, {0,0}, {1,0,0,1} },
						{ {  1,  1, 0 }, {0,0,1}, {1,0}, {1,0,0,1} },
						{ {  1, -1, 0 }, {0,0,1}, {1,1}, {1,0,0,1} },
					};
					std::vector<unsigned int> cpuIdx = { 0,1,2, 0,2,3 };
					model->mMeshes.push_back(Mesh(cpuVerts, cpuIdx, {}));
					
					models.push_back(std::move(model));
					successCount++;
					cout << "  -> Fallback quad created" << endl;
				}
			}
			catch (const std::exception& e)
			{
				cerr << "  -> Error loading scene: " << e.what() << endl;
				// Continue with next scene
			}
		}
		
		if (models.empty())
		{
			cerr << "Error: No valid scenes could be loaded from " << paths.size() << " file(s)." << endl;
			std::lock_guard<std::mutex> lock(mLoadingMutex);
			mLoadingSuccess = false;
			mLoadingComplete = true;
			return;
		}
		
		// Store loaded models (thread-safe)
		{
			std::lock_guard<std::mutex> lock(mLoadingMutex);
			mPendingModels = std::move(models);
			mLoadingSuccess = true;
		}
		
		cout << "Background loading completed: " << successCount << " / " << paths.size() 
		     << " scene(s) loaded successfully" << endl;
	}
	catch (const std::exception& e)
	{
		cerr << "Critical error during scene loading: " << e.what() << endl;
		std::lock_guard<std::mutex> lock(mLoadingMutex);
		mLoadingSuccess = false;
	}
	
	// Mark as complete
	mLoadingComplete = true;
}

void Application::UploadModelsToGPU()
{
	// This runs on main thread and can safely use DirectX resources
	if (mIsLoadingExtension)
	{
		cout << "Uploading extension models to GPU..." << endl;
	}
	else
	{
		cout << "Uploading models to GPU..." << endl;
	}
	
	std::vector<std::unique_ptr<Model>> models;
	{
		std::lock_guard<std::mutex> lock(mLoadingMutex);
		models = std::move(mPendingModels);
	}
	
	if (models.empty())
	{
		cerr << "No models to upload!" << endl;
		mCurrentState = UIManager::AppState::LoadingMenu;
		mIsLoadingInProgress = false;
		mLoadingComplete = false;
		return;
	}
	
	size_t totalModels = models.size();
	
	// Upload all models - Renderer will handle memory limits
	bool success;
	if (mIsLoadingExtension)
	{
		success = mRenderer.AddExtensionScenes(std::move(models));
	}
	else
	{
		success = mRenderer.LoadMultipleScenes(std::move(models));
	}
	
	if (success)
	{
		mSceneLoaded = true;
		mCurrentState = UIManager::AppState::Scene;
		InputManager::Instance.SetCursorLocked(true);
		mCameraManager.SetActive(true);
		
		// Get number of loaded models from renderer
		size_t loadedCount = mRenderer.GetLoadedModelsCount();
		size_t previousCount = mIsLoadingExtension ? (loadedCount - totalModels) : 0;
		size_t actuallyAdded = loadedCount - previousCount;
		
		if (actuallyAdded < totalModels)
		{
			cout << "Warning: Only " << actuallyAdded << " / " << totalModels 
			     << " scenes loaded due to memory constraints." << endl;
			
			// Show warning in UI
			std::string warningMsg;
			if (mIsLoadingExtension)
			{
				warningMsg = "Only " + std::to_string(actuallyAdded) + " out of " 
				           + std::to_string(totalModels) + " extension scenes could be added.\n\n"
				           + "The remaining scenes exceeded available GPU memory.";
			}
			else
			{
				warningMsg = "Only " + std::to_string(actuallyAdded) + " out of " 
				           + std::to_string(totalModels) + " scenes could be loaded.\n\n"
				           + "The remaining scenes exceeded available GPU memory.";
			}
			mUIManager.ShowWarning(warningMsg);
		}
		else
		{
			if (mIsLoadingExtension)
			{
				cout << "All " << totalModels << " extension scene(s) added successfully! (Total: " 
				     << loadedCount << " models)" << endl;
			}
			else
			{
				cout << "All " << totalModels << " scene(s) loaded successfully!" << endl;
			}
		}
	}
	else
	{
		cerr << "Failed to upload scenes to GPU" << endl;
		mCurrentState = mIsLoadingExtension ? UIManager::AppState::Menu : UIManager::AppState::LoadingMenu;
		
		// Show error in UI
		std::string errorMsg = mIsLoadingExtension 
		    ? "Failed to add extension scenes.\n\nThe scenes may be too large for available GPU memory."
		    : "Failed to load scenes.\n\nThe scenes may be too large for available GPU memory.";
		mUIManager.ShowWarning(errorMsg);
	}
	
	if (!mIsLoadingExtension)
	{
		mCurrentScenePath = (totalModels == 1) ? mPendingScenePaths[0] : "Multiple scenes";
	}
	
	mPendingScenePaths.clear();
	mIsLoadingInProgress = false;
	mLoadingComplete = false;
	mIsLoadingExtension = false;
}

void Application::ProcessSceneLoading()
{
	if (!mIsLoadingInProgress)
		return;

	// Check if loading is complete
	if (mLoadingComplete)
	{
		// Wait for thread to finish
		if (mLoadingThread.joinable())
			mLoadingThread.join();
		
		if (mLoadingSuccess)
		{
			// Upload to GPU on main thread (safe for DirectX)
			UploadModelsToGPU();
		}
		else
		{
			// Loading failed, return to menu
			cerr << "Failed to load scene(s)" << endl;
			
			mCurrentState = UIManager::AppState::LoadingMenu;
			mPendingScenePaths.clear();
			mIsLoadingInProgress = false;
			mLoadingComplete = false;
		}
	}
	// Otherwise, keep showing loading screen and processing messages
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
