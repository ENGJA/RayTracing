#pragma once
#include "RenderAPI/Renderer.h"
#include "RenderAPI/Camera/CameraManager.h"
#include "UI/UIManager.h"
#include "Utils/PerformanceMonitor.h"
#include <thread>
#include <atomic>
#include <mutex>
#include <memory>
#include <vector>

// Forward declare Model to avoid circular dependency
class Model;

/**
 * @brief Application bootstrap that owns the window and resources.
 */
class Application
{
private:
	Renderer mRenderer; ///< High level renderer instance.
	CameraManager mCameraManager; ///< Camera manager for view/projection matrices.
	UIManager mUIManager; ///< UI manager for ImGui windows.
	PerformanceMonitor mPerformanceMonitor; ///< Performance monitoring for FPS, CPU, GPU, RAM, VRAM.

	UIManager::AppState mCurrentState = UIManager::AppState::LoadingMenu; // Start with loading menu

	HWND mHwnd = nullptr; ///< Native Win32 window handle.
	bool mIsRunning = true; ///< Main loop running flag.
	UINT mWidth = 0; ///< Current client area width in pixels.
	UINT mHeight = 0; ///< Current client area height in pixels.

	LARGE_INTEGER mPrevCounter{}; ///< Previous frame timestamp.
	double mSecondsPerCount = 0.0; ///< Seconds per performance counter tick.

	std::string mCurrentScenePath; ///< Currently loaded scene path.
	bool mSceneLoaded = false; ///< Whether a scene is currently loaded.
	
	// Async loading
	std::thread mLoadingThread; ///< Background loading thread
	std::atomic<bool> mIsLoadingInProgress = false; ///< Whether loading is currently in progress
	std::atomic<bool> mLoadingComplete = false; ///< Whether loading has completed
	std::atomic<bool> mLoadingSuccess = false; ///< Whether loading was successful
	std::mutex mLoadingMutex; ///< Mutex for thread-safe loading operations
	std::unique_ptr<Model> mPendingModel; ///< Model loaded on background thread
	std::vector<std::unique_ptr<Model>> mPendingModels; ///< Models loaded on background thread (for multi-load)
	std::vector<std::string> mPendingScenePaths; ///< Paths being loaded (used for both single and multi-load)
	
	// Application logic
	void ToggleMenu();
	void LoadScene(const std::string& path);
	void LoadMultipleScenes(const std::vector<std::string>& paths);
	void ProcessSceneLoading();
	void PerformAsyncMultiLoad(const std::vector<std::string>& paths);
	void UploadModelsToGPU();
	void UnloadScene();
	void ExitToMainMenu();
	void ExitApplication();

public:
	/**
	 * @brief Checks if the application main loop should continue running.
	 * @return true while running, otherwise false.
	 */
	bool IsRunning() const { return mIsRunning; }

	/**
	 * @brief Creates the OS window and initializes the renderer.
	 * @param className Win32 window class name.
	 * @param windowName Window title.
	 * @param width Client width in pixels.
	 * @param height Client height in pixels.
	 * @return true on success, false otherwise.
	 */
	bool Initialize(LPCWSTR className, LPCWSTR windowName, int width = 1280, int height = 720);

	/**
	 * @brief Per-frame update and render.
	 */
	void Update();

	/**
	 * @brief Win32 create callback.
	 * @param hwnd Window handle.
	 */
	void OnCreate(HWND hwnd);

	/**
	 * @brief Win32 destroy callback.
	 */
	void OnDestroy();

	/**
	 * @brief Called on window resize.
	 */
	void OnResize(int width, int height);
};

