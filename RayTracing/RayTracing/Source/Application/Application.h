#pragma once
#include "RenderAPI/Renderer.h"
#include "RenderAPI/Camera/CameraManager.h"

enum class AppState
{
	Scene,  ///< Renderowanie 3D, sterowanie kamer¹, brak kursora.
	Menu    ///< Pauza/Menu, widoczny kursor, brak sterowania kamer¹.
};

/**
 * @brief Application bootstrap that owns the window and resources.
 */
class Application
{
private:
	Renderer mRenderer; ///< High level renderer instance.
	CameraManager mCameraManager; ///< Camera manager for view/projection matrices.

	AppState mCurrentState = AppState::Scene;

	HWND mHwnd = nullptr; ///< Native Win32 window handle.
	bool mIsRunning = true; ///< Main loop running flag.
	UINT mWidth = 0; ///< Current client area width in pixels.
	UINT mHeight = 0; ///< Current client area height in pixels.

	LARGE_INTEGER mPrevCounter{}; ///< Previous frame timestamp.
	double mSecondsPerCount = 0.0; ///< Seconds per performance counter tick.

	void ToggleMenu();
	void RenderImGuiMenu();

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

