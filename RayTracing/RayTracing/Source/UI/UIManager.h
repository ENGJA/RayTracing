#pragma once
#include <string>
#include <functional>

// Forward declarations
class CameraManager;
class PerformanceMonitor;

/**
 * @brief Manages all ImGui UI rendering and state.
 */
class UIManager
{
public:
	/**
	 * @brief Callback types for UI actions.
	 */
	using LoadSceneCallback = std::function<void(const std::string& path)>;
	using UnloadSceneCallback = std::function<void()>;
	using ToggleMenuCallback = std::function<void()>;
	using ExitCallback = std::function<void()>;
	using ExitToMainMenuCallback = std::function<void()>;

	/**
	 * @brief Application states for UI rendering.
	 */
	enum class AppState
	{
		LoadingMenu,
		Menu,
		Scene,
		LoadingScene
	};

	/**
	 * @brief Initializes UI manager with required dependencies.
	 * @param hwnd Window handle for file dialogs.
	 * @param cameraManager Camera manager for settings.
	 * @param perfMonitor Performance monitor for stats display.
	 */
	void Initialize(HWND hwnd, CameraManager* cameraManager, PerformanceMonitor* perfMonitor = nullptr);

	/**
	 * @brief Renders all UI windows based on current state.
	 * @param currentState Current application state.
	 */
	void RenderUI(AppState currentState);

	/**
	 * @brief Set callback for loading a scene.
	 */
	void SetLoadSceneCallback(LoadSceneCallback callback) { mLoadSceneCallback = callback; }

	/**
	 * @brief Set callback for unloading a scene.
	 */
	void SetUnloadSceneCallback(UnloadSceneCallback callback) { mUnloadSceneCallback = callback; }

	/**
	 * @brief Set callback for toggling menu.
	 */
	void SetToggleMenuCallback(ToggleMenuCallback callback) { mToggleMenuCallback = callback; }

	/**
	 * @brief Set callback for exiting application.
	 */
	void SetExitCallback(ExitCallback callback) { mExitCallback = callback; }

	/**
	 * @brief Set callback for exiting to main menu.
	 */
	void SetExitToMainMenuCallback(ExitToMainMenuCallback callback) { mExitToMainMenuCallback = callback; }

	/**
	 * @brief Toggle settings window visibility.
	 */
	void ToggleSettings() { mShowSettingsWindow = !mShowSettingsWindow; }

	/**
	 * @brief Toggle performance overlay visibility.
	 */
	void TogglePerformanceOverlay() { mShowPerformanceOverlay = !mShowPerformanceOverlay; }

	/**
	 * @brief Check if any UI window is open in scene mode (requiring cursor).
	 */
	bool IsAnyWindowOpen() const { return mShowSettingsWindow || mShowAboutWindow || mShowControlsWindow; }

	/**
	 * @brief Set the scene name being loaded for display in loading screen.
	 * @param sceneName Name of the scene file being loaded.
	 */
	void SetLoadingSceneName(const std::string& sceneName) { mLoadingSceneName = sceneName; }

private:
	// Rendering methods
	void RenderLoadingMenu();
	void RenderPauseMenu();
	void RenderSettingsWindow();
	void RenderAboutWindow();
	void RenderControlsWindow();
	void RenderPerformanceOverlay();
	void RenderLoadingScene();

	// Helper methods
	bool OpenFileDialog(std::string& outPath);

	// Dependencies
	HWND mHwnd = nullptr;
	CameraManager* mCameraManager = nullptr;
	PerformanceMonitor* mPerformanceMonitor = nullptr;

	// UI state
	bool mShowAboutWindow = false;
	bool mShowControlsWindow = false;
	bool mShowSettingsWindow = false;
	bool mShowPerformanceOverlay = true;
	std::string mLoadingSceneName;

	// Callbacks
	LoadSceneCallback mLoadSceneCallback;
	UnloadSceneCallback mUnloadSceneCallback;
	ToggleMenuCallback mToggleMenuCallback;
	ExitCallback mExitCallback;
	ExitToMainMenuCallback mExitToMainMenuCallback;
};
