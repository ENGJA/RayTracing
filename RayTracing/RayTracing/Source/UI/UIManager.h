#pragma once
#include <string>
#include <functional>
#include <vector>

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
	using LoadMultipleScenesCallback = std::function<void(const std::vector<std::string>& paths)>;
	using AddExtensionScenesCallback = std::function<void(const std::vector<std::string>& paths)>;
	using UnloadSceneCallback = std::function<void()>;
	using ToggleMenuCallback = std::function<void()>;
	using ExitCallback = std::function<void()>;
	using ExitToMainMenuCallback = std::function<void()>;
	using SetDLSSModeCallback = std::function<void(int)>;

	using SetShadowsEnabledCallback = std::function<void(bool)>;
	using GetShadowsEnabledCallback = std::function<bool()>;

	using SetReflectionsEnabledCallback = std::function<void(bool)>;
	using GetReflectionsEnabledCallback = std::function<bool()>;

	using SetMaxReflectionDepthCallback = std::function<void(UINT)>;
	using GetMaxReflectionDepthCallback = std::function<UINT()>;
	using SetMaxTransmissionDepthCallback = std::function<void(UINT)>;
	using GetMaxTransmissionDepthCallback = std::function<UINT()>;

	using SetSunDirectionCallback = std::function<void(float, float, float)>;
	using GetSunDirectionCallback = std::function<DirectX::XMFLOAT3()>;

	using SetSunColorCallback = std::function<void(float, float, float)>;
	using GetSunColorCallback = std::function<DirectX::XMFLOAT3()>;
	using SetSunEnabledCallback = std::function<void(bool)>;
	using GetSunEnabledCallback = std::function<bool()>;

	using SetRISCandidatesCallback = std::function<void(UINT)>;
	using GetRISCandidatesCallback = std::function<UINT()>;
	using GetNumPointLightsCallback = std::function<UINT()>;

	using SetShadowRaysCallback = std::function<void(UINT)>;
	using GetShadowRaysCallback = std::function<UINT()>;

	using SetRenderModeCallback = std::function<void(int)>;
	using GetRenderModeCallback = std::function<int()>;

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
	 * @brief Set callback for loading multiple scenes.
	 */
	void SetLoadMultipleScenesCallback(LoadMultipleScenesCallback callback) { mLoadMultipleScenesCallback = callback; }

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
	 * @brief Set callback for adding extension scenes to current scene.
	 */
	void SetAddExtensionScenesCallback(AddExtensionScenesCallback callback) { mAddExtensionScenesCallback = callback; }

	/**
	 * @brief Set callback for changing DLSS mode.
	 */
	void SetSetDLSSModeCallback(SetDLSSModeCallback callback) { mSetDLSSModeCallback = callback; }

	/**
	 * @brief Set current DLSS mode index for UI display.
	 */
	void SetCurrentDLSSMode(int mode) { mCurrentDLSSMode = mode; }

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
	
	/**
	 * @brief Show a warning message in the UI (e.g., for memory limits).
	 * @param message Warning message to display.
	 */
	void ShowWarning(const std::string& message) { mWarningMessage = message; mShowWarning = true; }
	
	/**
	 * @brief Clear the current warning message.
	 */
	void ClearWarning() { mShowWarning = false; mWarningMessage.clear(); }


	void SetShadowsCallbacks(GetShadowsEnabledCallback get, SetShadowsEnabledCallback set)
	{
		mGetShadowsEnabled = get;
		mSetShadowsEnabled = set;
	}

	void SetReflectionsCallbacks(GetReflectionsEnabledCallback get, SetReflectionsEnabledCallback set)
	{
		mGetReflectionsEnabled = get;
		mSetReflectionsEnabled = set;
	}

	void SetMaxReflectionDepthCallbacks(GetMaxReflectionDepthCallback get, SetMaxReflectionDepthCallback set)
	{
		mGetMaxRecursionDepth = get;
		mSetMaxRecursionDepth = set;
	}

	void SetMaxTransmissionDepthCallbacks(GetMaxTransmissionDepthCallback get, SetMaxTransmissionDepthCallback set)
	{
		mGetMaxTransmissionDepth = get;
		mSetMaxTransmissionDepth = set;
	}


	void SetSunDirectionCallbacks(GetSunDirectionCallback get, SetSunDirectionCallback set)
	{
		mGetSunDirection = get;
		mSetSunDirection = set;
	}

	void SetSunColorCallbacks(GetSunColorCallback get, SetSunColorCallback set)
	{
		mGetSunColor = get;
		mSetSunColor = set;
	}

	void SetSunEnabledCallbacks(GetSunEnabledCallback get, SetSunEnabledCallback set)
	{
		mGetSunEnabled = get;
		mSetSunEnabled = set;
	}

	void SetRISCandidatesCallbacks(GetRISCandidatesCallback get, SetRISCandidatesCallback set, GetNumPointLightsCallback getCount)
	{
		mGetRISCandidates = get;
		mSetRISCandidates = set;
		mGetNumPointLights = getCount;
	}

	void SetShadowRaysCallbacks(GetShadowRaysCallback get, SetShadowRaysCallback set)
	{
		mGetShadowRays = get;
		mSetShadowRays = set;
	}

	void SetRenderModeCallbacks(GetRenderModeCallback get, SetRenderModeCallback set)
	{
		mGetRenderMode = get;
		mSetRenderMode = set;
	}

private:
	// Rendering methods
	void RenderLoadingMenu();
	void RenderMultiSceneSelectionWindow();
	void RenderPauseMenu();
	void RenderSettingsWindow();
	void RenderAboutWindow();
	void RenderControlsWindow();
	void RenderPerformanceOverlay();
	void RenderLoadingScene();
	void RenderWarningWindow();

	// Helper methods
	bool OpenFileDialog(std::string& outPath);
	bool OpenMultiFileDialog(std::vector<std::string>& outPaths);

	// Dependencies
	HWND mHwnd = nullptr;
	CameraManager* mCameraManager = nullptr;
	PerformanceMonitor* mPerformanceMonitor = nullptr;

	// UI state
	bool mShowAboutWindow = false;
	bool mShowControlsWindow = false;
	bool mShowSettingsWindow = false;
	bool mShowPerformanceOverlay = true;
	bool mShowMultiSceneSelectionWindow = false;
	bool mIsExtensionMode = false;
	bool mShowWarning = false;
	std::string mLoadingSceneName;
	std::string mWarningMessage;
	std::vector<std::string> mSelectedScenePaths;

	// Callbacks
	LoadSceneCallback mLoadSceneCallback;
	LoadMultipleScenesCallback mLoadMultipleScenesCallback;
	AddExtensionScenesCallback mAddExtensionScenesCallback;
	UnloadSceneCallback mUnloadSceneCallback;
	ToggleMenuCallback mToggleMenuCallback;
	ExitCallback mExitCallback;
	ExitToMainMenuCallback mExitToMainMenuCallback;
	SetDLSSModeCallback mSetDLSSModeCallback;

	GetShadowsEnabledCallback mGetShadowsEnabled;
	SetShadowsEnabledCallback mSetShadowsEnabled;

	GetReflectionsEnabledCallback mGetReflectionsEnabled;
	SetReflectionsEnabledCallback mSetReflectionsEnabled;

	GetMaxReflectionDepthCallback mGetMaxRecursionDepth;
	SetMaxReflectionDepthCallback mSetMaxRecursionDepth;

	GetMaxTransmissionDepthCallback mGetMaxTransmissionDepth;
	SetMaxTransmissionDepthCallback mSetMaxTransmissionDepth;

	GetSunDirectionCallback mGetSunDirection;
	SetSunDirectionCallback mSetSunDirection;

	GetSunColorCallback mGetSunColor;
	SetSunColorCallback mSetSunColor;
	GetSunEnabledCallback mGetSunEnabled;
	SetSunEnabledCallback mSetSunEnabled;

	GetRISCandidatesCallback mGetRISCandidates;
	SetRISCandidatesCallback mSetRISCandidates;
	GetNumPointLightsCallback mGetNumPointLights;

	GetShadowRaysCallback mGetShadowRays;
	SetShadowRaysCallback mSetShadowRays;

	GetRenderModeCallback mGetRenderMode;
	SetRenderModeCallback mSetRenderMode;

	int mCurrentDLSSMode = 0;
};
