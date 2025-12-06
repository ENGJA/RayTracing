#pragma once
#include <string>
#include <functional>

// Forward declarations
class CameraManager;

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
		Scene
	};

	/**
	 * @brief Initializes UI manager with required dependencies.
	 * @param hwnd Window handle for file dialogs.
	 * @param cameraManager Camera manager for settings.
	 */
	void Initialize(HWND hwnd, CameraManager* cameraManager);

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

private:
	// Rendering methods
	void RenderLoadingMenu();
	void RenderPauseMenu();
	void RenderSettingsWindow();
	void RenderAboutWindow();
	void RenderControlsWindow();

	// Helper methods
	bool OpenFileDialog(std::string& outPath);

	// Dependencies
	HWND mHwnd = nullptr;
	CameraManager* mCameraManager = nullptr;

	// UI state
	bool mShowAboutWindow = false;
	bool mShowControlsWindow = false;
	bool mShowSettingsWindow = false;

	// Callbacks
	LoadSceneCallback mLoadSceneCallback;
	UnloadSceneCallback mUnloadSceneCallback;
	ToggleMenuCallback mToggleMenuCallback;
	ExitCallback mExitCallback;
	ExitToMainMenuCallback mExitToMainMenuCallback;
};
