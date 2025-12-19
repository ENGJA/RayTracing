#include "pch.h"
#include "UIManager.h"
#include "RenderAPI/Camera/CameraManager.h"
#include "Utils/PerformanceMonitor.h"
#include "imgui.h"

void UIManager::Initialize(HWND hwnd, CameraManager* cameraManager, PerformanceMonitor* perfMonitor)
{
	mHwnd = hwnd;
	mCameraManager = cameraManager;
	mPerformanceMonitor = perfMonitor;
}

void UIManager::RenderUI(AppState currentState)
{
	// Render appropriate UI based on state
	if (currentState == AppState::LoadingMenu)
	{
		RenderLoadingMenu();
	}
	else if (currentState == AppState::Menu)
	{
		RenderPauseMenu();
		
		// Settings can be shown in pause menu too
		if (mShowSettingsWindow)
			RenderSettingsWindow();
	}
	else if (currentState == AppState::LoadingScene)
	{
		RenderLoadingScene();
	}
	else // Scene mode
	{
		// Render performance overlay in scene mode (if enabled)
		if (mShowPerformanceOverlay)
			RenderPerformanceOverlay();

		// Render settings window in scene mode (can be toggled)
		if (mShowSettingsWindow)
			RenderSettingsWindow();
	}
	
	// These windows can be shown in any mode
	if (mShowAboutWindow)
		RenderAboutWindow();
	if (mShowControlsWindow)
		RenderControlsWindow();
	if (mShowMultiSceneSelectionWindow)
		RenderMultiSceneSelectionWindow();
	if (mShowWarning)
		RenderWarningWindow();
}

void UIManager::RenderLoadingMenu()
{
	ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(400, 230), ImGuiCond_Always);

	ImGui::Begin("Welcome", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

	// Title text
	ImGui::SetCursorPos(ImVec2(15.0f, 10.0f));
	ImGui::Text("3D SCENE VIEWER");
	
	ImGui::SetCursorPos(ImVec2(15.0f, 30.0f));
	ImGui::Separator();
	
	// Fixed absolute positions
	float x = 15.0f;
	
	// Load Multiple Scenes button at Y = 50
	ImGui::SetCursorPos(ImVec2(x, 50.0f));
	if (ImGui::Button("Load Scene(s)...", ImVec2(370, 50)))
	{
		// Open scene selection window with empty list
		mSelectedScenePaths.clear();
		mIsExtensionMode = false;
		mShowMultiSceneSelectionWindow = true;
	}
	
	// About and Controls buttons at Y = 110
	ImGui::SetCursorPos(ImVec2(x, 110.0f));
	if (ImGui::Button("About", ImVec2(180, 35)))
	{
		mShowAboutWindow = true;
	}
	
	ImGui::SetCursorPos(ImVec2(x + 190.0f, 110.0f));
	if (ImGui::Button("Controls", ImVec2(180, 35)))
	{
		mShowControlsWindow = true;
	}
	
	// Exit button at Y = 155
	ImGui::SetCursorPos(ImVec2(x, 155.0f));
	if (ImGui::Button("Exit", ImVec2(370, 50)))
	{
		if (mExitCallback)
			mExitCallback();
	}

	ImGui::End();
}

void UIManager::RenderPauseMenu()
{
	ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(400, 510), ImGuiCond_Always);

	ImGui::Begin("Menu", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

	// Title text instead of titlebar
	ImGui::SetCursorPos(ImVec2(15.0f, 10.0f));
	ImGui::Text("MENU - PAUSED");
	
	ImGui::SetCursorPos(ImVec2(15.0f, 30.0f));
	ImGui::Separator();
	
	// Absolute positioning from top of window content
	float buttonX = 15.0f;
	float buttonWidth = 370.0f;
	float buttonHeight = 40.0f;
	float gap = 10.0f;
	
	// Start at fixed Y position
	float y = 50.0f;
	
	// Button 1: Resume
	ImGui::SetCursorPos(ImVec2(buttonX, y));
	if (ImGui::Button("Resume (ESC)", ImVec2(buttonWidth, buttonHeight)))
	{
		if (mToggleMenuCallback)
			mToggleMenuCallback();
	}
	y += buttonHeight + gap;
	
	// Button 2: Settings
	ImGui::SetCursorPos(ImVec2(buttonX, y));
	if (ImGui::Button("Settings", ImVec2(buttonWidth, buttonHeight)))
	{
		mShowSettingsWindow = !mShowSettingsWindow;
	}
	y += buttonHeight + gap;
	
	// Button 3: About and Controls (side by side)
	float smallButtonWidth = (buttonWidth - gap) / 2.0f;
	
	ImGui::SetCursorPos(ImVec2(buttonX, y));
	if (ImGui::Button("About", ImVec2(smallButtonWidth, buttonHeight)))
	{
		mShowAboutWindow = true;
	}
	
	ImGui::SetCursorPos(ImVec2(buttonX + smallButtonWidth + gap, y));
	if (ImGui::Button("Controls", ImVec2(smallButtonWidth, buttonHeight)))
	{
		mShowControlsWindow = true;
	}
	y += buttonHeight + gap;
	
	// Button 4: Add Extension Scene(s)
	ImGui::SetCursorPos(ImVec2(buttonX, y));
	if (ImGui::Button("Add Extension Scene(s)...", ImVec2(buttonWidth, buttonHeight)))
	{
		// Open scene selection window with empty list in extension mode
		mSelectedScenePaths.clear();
		mIsExtensionMode = true;
		mShowMultiSceneSelectionWindow = true;
	}
	y += buttonHeight + gap;
	
	// Button 5: Load Different Scene(s)
	ImGui::SetCursorPos(ImVec2(buttonX, y));
	if (ImGui::Button("Load Different Scene(s)...", ImVec2(buttonWidth, buttonHeight)))
	{
		// Open scene selection window with empty list for replacement
		mSelectedScenePaths.clear();
		mIsExtensionMode = false;
		mShowMultiSceneSelectionWindow = true;
	}
	y += buttonHeight + gap;
	
	// Button 6: Exit to Menu
	ImGui::SetCursorPos(ImVec2(buttonX, y));
	if (ImGui::Button("Exit to Main Menu", ImVec2(buttonWidth, buttonHeight)))
	{
		if (mExitToMainMenuCallback)
			mExitToMainMenuCallback();
	}
	y += buttonHeight + gap;
	
	// Button 7: Exit App
	ImGui::SetCursorPos(ImVec2(buttonX, y));
	if (ImGui::Button("Exit Application", ImVec2(buttonWidth, buttonHeight)))
	{
		if (mExitCallback)
			mExitCallback();
	}

	ImGui::End();
}

void UIManager::RenderSettingsWindow()
{
	ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(450, 330), ImGuiCond_Once);

	if (!ImGui::Begin("Settings", &mShowSettingsWindow, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize))
	{
		ImGui::End();
		return;
	}

	if (!mCameraManager)
	{
		ImGui::Text("Camera manager not available.");
		ImGui::End();
		return;
	}

	// Camera speed control
	static float currentSpeed = 1.0f;
	currentSpeed = mCameraManager->GetMoveSpeedMultiplier();
	
	ImGui::Text("Camera Settings");
	ImGui::Separator();
	ImGui::Spacing();
	
	ImGui::Text("Movement Speed: %.1fx", currentSpeed);
	
	if (ImGui::SliderFloat("##speed", &currentSpeed, 0.1f, 5.0f, "%.1fx"))
	{
		mCameraManager->SetMoveSpeedMultiplier(currentSpeed);
	}
	
	ImGui::Spacing();
	
	// Get current active camera
	Camera& activeCamera = mCameraManager->GetActiveCamera();
	
	// Near plane slider
	float nearZ = activeCamera.GetNearZ();
	ImGui::Text("Near Plane: %.3f", nearZ);
	if (ImGui::SliderFloat("##near", &nearZ, 0.001f, 10.0f, "%.3f"))
	{
		// Clamp to ensure near < far
		float farZ = activeCamera.GetFarZ();
		if (nearZ >= farZ)
			nearZ = farZ - 0.01f;
		activeCamera.SetNearZ(nearZ);
	}
	
	ImGui::Spacing();
	
	// Far plane slider
	float farZ = activeCamera.GetFarZ();
	ImGui::Text("Far Plane: %.1f", farZ);
	if (ImGui::SliderFloat("##far", &farZ, 1.0f, 1000.0f, "%.1f"))
	{
		// Clamp to ensure far > near
		float nearZ = activeCamera.GetNearZ();
		if (farZ <= nearZ)
			farZ = nearZ + 0.01f;
		activeCamera.SetFarZ(farZ);
	}
	
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Text("DLSS Settings");
	
	// DLSS Modes corresponding to sl::DLSSMode enum:
	// eOff = 0, eMaxPerformance = 1, eBalanced = 2, eMaxQuality = 3, 
	// eUltraPerformance = 4, eUltraQuality = 5, eDLAA = 6
	const char* dlssModes[] = { 
		//"Off",                // 0
		"Max Performance",    // 1
		"Balanced",           // 2
		"Max Quality",        // 3
		"Ultra Performance",  // 4
		"Ultra Quality",      // 5
		"DLAA"                // 6
	};
	
	// Current mode directly maps to array index
	int uiModeIndex = mCurrentDLSSMode;
	if (uiModeIndex < 0 || uiModeIndex >= IM_ARRAYSIZE(dlssModes))
		uiModeIndex = 0; // Default to Off if out of range

	if (ImGui::Combo("DLSS Mode", &uiModeIndex, dlssModes, IM_ARRAYSIZE(dlssModes)))
	{
		mCurrentDLSSMode = uiModeIndex + 1; // Direct mapping
		if (mSetDLSSModeCallback)
			mSetDLSSModeCallback(mCurrentDLSSMode);
	}

	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// Centered close button
	float windowWidth = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
	float buttonWidth = 150.0f;
	ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f + ImGui::GetWindowContentRegionMin().x);
	
	if (ImGui::Button("Close", ImVec2(buttonWidth, 50)))
	{
		mShowSettingsWindow = false;
	}

	ImGui::End();
}

void UIManager::RenderAboutWindow()
{
	ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_Once);

	if (!ImGui::Begin("About 3D Scene Viewer", &mShowAboutWindow, ImGuiWindowFlags_NoResize))
	{
		ImGui::End();
		return;
	}

	ImGui::TextWrapped("3D Scene Viewer");
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::TextWrapped("A real-time 3D scene renderer built with DirectX 12.");
	ImGui::Spacing();

	ImGui::Text("Features:");
	ImGui::BulletText("PBR (Physically Based Rendering) materials");
	ImGui::BulletText("Dynamic lighting with multiple light sources");
	ImGui::BulletText("Free camera movement and rotation");
	ImGui::BulletText("Support for GLTF, OBJ, FBX file formats");
	ImGui::BulletText("Real-time mipmap generation");
	ImGui::BulletText("ImGui interface for easy control");
	
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::TextWrapped("Built with: DirectX 12, Assimp, ImGui, DirectXMath");
	
	ImGui::Spacing();
	
	if (ImGui::Button("Close", ImVec2(120, 30)))
	{
		mShowAboutWindow = false;
	}

	ImGui::End();
}

void UIManager::RenderControlsWindow()
{
	ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_Once);
	ImGui::SetNextWindowSize(ImVec2(500, 450), ImGuiCond_Once);

	if (!ImGui::Begin("Controls", &mShowControlsWindow, ImGuiWindowFlags_NoResize))
	{
		ImGui::End();
		return;
	}

	ImGui::TextWrapped("Camera Controls");
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Text("Movement:");
	ImGui::BulletText("W / S - Move forward / backward");
	ImGui::BulletText("A / D - Move left / right");
	ImGui::BulletText("Space / Shift - Move up / down");
	
	ImGui::Spacing();

	ImGui::Text("Rotation:");
	ImGui::BulletText("Mouse Movement - Look around (when in scene)");
	ImGui::BulletText("Arrow Keys - Rotate camera");
	
	ImGui::Spacing();

	ImGui::Text("Zoom:");
	ImGui::BulletText("Z / X - Decrease / Increase FOV");
	ImGui::BulletText("Mouse Wheel - Adjust FOV");
	
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::Text("General:");
	ImGui::BulletText("ESC - Pause / Resume (toggle menu)");
	ImGui::BulletText("F1 - Open Settings (in scene mode)");
	ImGui::BulletText("F2 - Toggle Performance Stats (in scene mode)");
	ImGui::BulletText("C - Toggle between cameras");
	ImGui::BulletText("G - Reload shaders (debug)");
	
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::TextWrapped("Tip: Adjust camera speed in the Settings menu for more comfortable navigation.");
	
	ImGui::Spacing();
	
	if (ImGui::Button("Close", ImVec2(120, 30)))
	{
		mShowControlsWindow = false;
	}

	ImGui::End();
}

void UIManager::RenderPerformanceOverlay()
{
	if (!mPerformanceMonitor)
		return;

	const float DISTANCE = 10.0f;
	ImVec2 windowPos = ImVec2(DISTANCE, DISTANCE);
	ImVec2 windowPosPivot = ImVec2(0.0f, 0.0f);

	ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always, windowPosPivot);
	ImGui::SetNextWindowBgAlpha(0.35f);

	ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoDecoration | 
	                                ImGuiWindowFlags_AlwaysAutoResize | 
	                                ImGuiWindowFlags_NoSavedSettings | 
	                                ImGuiWindowFlags_NoFocusOnAppearing | 
	                                ImGuiWindowFlags_NoNav | 
	                                ImGuiWindowFlags_NoMove;

	if (ImGui::Begin("Performance", nullptr, windowFlags))
	{
		ImGui::Text("Performance Stats (F2 to toggle)");
		ImGui::Separator();
		
		float fps = mPerformanceMonitor->GetFPS();
		float frameTime = mPerformanceMonitor->GetAvgFrameTimeMS();
		float cpuUsage = mPerformanceMonitor->GetCPUUsage();
		float gpuUsage = mPerformanceMonitor->GetGPUUsage();
		float gpu3DUsage = mPerformanceMonitor->GetGPU3DUsage();
		float ramUsage = mPerformanceMonitor->GetRAMUsageMB();
		float vramUsage = mPerformanceMonitor->GetVRAMUsageMB();

		// FPS with color coding
		if (fps >= 60.0f)
			ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "FPS: %.1f", fps);
		else if (fps >= 30.0f)
			ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "FPS: %.1f", fps);
		else
			ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "FPS: %.1f", fps);
		
		ImGui::Text("Frame Time: %.2f ms", frameTime);
		ImGui::Separator();
		
		ImGui::Text("CPU: %.1f%%", cpuUsage);
		
		// Display GPU usage (prefer 3D usage, fallback to general GPU usage)
		if (gpu3DUsage > 0.0f)
		{
			ImGui::Text("GPU: %.1f%%", gpu3DUsage);
		}
		else if (gpuUsage > 0.0f)
		{
			ImGui::Text("GPU: %.1f%%", gpuUsage);
		}
		else
		{
			ImGui::TextDisabled("GPU: N/A");
		}
		
		ImGui::Separator();
		ImGui::Text("RAM: %.1f MB", ramUsage);
		
		if (vramUsage > 0.0f)
			ImGui::Text("VRAM: %.1f MB", vramUsage);
		else
			ImGui::TextDisabled("VRAM: N/A");
	}
	ImGui::End();
}

void UIManager::RenderLoadingScene()
{
	ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(400, 180), ImGuiCond_Always);

	ImGui::Begin("Loading", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

	// Title
	ImGui::SetCursorPos(ImVec2(15.0f, 20.0f));
	ImGui::Text("LOADING SCENE");
	
	ImGui::SetCursorPos(ImVec2(15.0f, 45.0f));
	ImGui::Separator();
	
	// Scene name
	ImGui::SetCursorPos(ImVec2(15.0f, 60.0f));
	if (!mLoadingSceneName.empty())
	{
		ImGui::TextWrapped("%s", mLoadingSceneName.c_str());
	}
	else
	{
		ImGui::Text("Please wait...");
	}
	
	// Loading animation (simple dots)
	ImGui::SetCursorPos(ImVec2(15.0f, 110.0f));
	static float loadingTime = 0.0f;
	loadingTime += ImGui::GetIO().DeltaTime;
	int dots = (int)(loadingTime * 2.0f) % 4;
	std::string loadingText = "Loading";
	for (int i = 0; i < dots; i++)
		loadingText += ".";
	ImGui::Text("%s", loadingText.c_str());
	
	// Additional info
	ImGui::SetCursorPos(ImVec2(15.0f, 135.0f));
	ImGui::TextDisabled("This may take a moment for large scenes");

	ImGui::End();
}

void UIManager::RenderMultiSceneSelectionWindow()
{
	ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(650, 500), ImGuiCond_Appearing);

	std::string windowTitle = mIsExtensionMode ? "Add Extension Scene(s)" : "Scene Selection";
	if (!ImGui::Begin(windowTitle.c_str(), &mShowMultiSceneSelectionWindow, ImGuiWindowFlags_NoCollapse))
	{
		ImGui::End();
		return;
	}

	if (mIsExtensionMode)
	{
		ImGui::TextWrapped("Select additional scene files to add to the current scene (e.g., add decorations to an existing building).");
	}
	else
	{
		ImGui::TextWrapped("Select scene files to load together (e.g., Sponza palace + curtains).");
	}
	ImGui::Spacing();
	
	if (mSelectedScenePaths.empty())
	{
		ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.0f, 1.0f), "Click 'Add Files...' to start adding scene files.");
	}
	
	ImGui::Separator();
	ImGui::Spacing();

	// Display list of selected files with delete buttons
	ImGui::Text("Selected Files (%zu):", mSelectedScenePaths.size());
	ImGui::BeginChild("FileList", ImVec2(0, -110), true, ImGuiWindowFlags_AlwaysVerticalScrollbar);
	
	if (mSelectedScenePaths.empty())
	{
		// Show helpful message when list is empty
		ImVec2 childSize = ImGui::GetWindowSize();
		ImGui::SetCursorPosY(childSize.y * 0.4f);
		
		ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.6f, 0.6f, 0.6f, 1.0f));
		ImGui::TextWrapped("No files added yet.\n\nClick 'Add Files...' button below to select scene files.");
		ImGui::PopStyleColor();
	}
	else
	{
		std::vector<int> toRemove;
		for (size_t i = 0; i < mSelectedScenePaths.size(); ++i)
		{
			ImGui::PushID(static_cast<int>(i));
			
			std::filesystem::path filePath(mSelectedScenePaths[i]);
			std::string filename = filePath.filename().string();
			std::string directory = filePath.parent_path().filename().string();
			
			// Display index and filename
			ImGui::Text("%zu.", i + 1);
			ImGui::SameLine();
			
			// Display filename with directory hint
			if (!directory.empty())
			{
				ImGui::BulletText("%s", filename.c_str());
				ImGui::SameLine();
				ImGui::TextDisabled("(%s)", directory.c_str());
			}
			else
			{
				ImGui::BulletText("%s", filename.c_str());
			}
			
			// Tooltip with full path
			if (ImGui::IsItemHovered())
			{
				ImGui::BeginTooltip();
				ImGui::TextUnformatted("Full path:");
				ImGui::TextWrapped("%s", mSelectedScenePaths[i].c_str());
				ImGui::EndTooltip();
			}
			
			ImGui::SameLine();
			float cursorX = ImGui::GetCursorPosX();
			ImGui::SetCursorPosX(ImGui::GetWindowWidth() - 80);
			if (ImGui::SmallButton("Remove"))
			{
				toRemove.push_back(static_cast<int>(i));
			}
			
			ImGui::PopID();
		}
		
		// Remove marked files (in reverse order to avoid index issues)
		for (auto it = toRemove.rbegin(); it != toRemove.rend(); ++it)
		{
			mSelectedScenePaths.erase(mSelectedScenePaths.begin() + *it);
		}
	}
	
	ImGui::EndChild();

	ImGui::Spacing();
	
	// Info text
	if (mSelectedScenePaths.empty())
	{
		ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.0f, 1.0f), "No files selected. Add scene files to continue.");
	}
	else
	{
		ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "%zu file(s) ready to load", mSelectedScenePaths.size());
		ImGui::SameLine();
		ImGui::TextDisabled("(?)");
		if (ImGui::IsItemHovered())
		{
			ImGui::BeginTooltip();
			if (mIsExtensionMode)
			{
				ImGui::TextUnformatted("Scenes will be added to the existing scene.");
				ImGui::TextUnformatted("The current scene will remain loaded.");
			}
			else
			{
				ImGui::TextUnformatted("Scenes will be loaded in order.");
				ImGui::TextUnformatted("Current scene will be unloaded first.");
			}
			ImGui::TextUnformatted("If GPU memory limit is reached, remaining scenes will be skipped.");
			ImGui::EndTooltip();
		}
	}
	
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	// Buttons at the bottom
	float buttonWidth = 140.0f;
	float gap = 8.0f;
	float totalWidth = buttonWidth * 3 + gap * 2;
	float startX = (ImGui::GetWindowWidth() - totalWidth) * 0.5f;

	ImGui::SetCursorPosX(startX);
	if (ImGui::Button("Add Files...", ImVec2(buttonWidth, 40)))
	{
		std::vector<std::string> additionalPaths;
		if (OpenMultiFileDialog(additionalPaths))
		{
			// Add new paths, avoiding duplicates
			for (const auto& newPath : additionalPaths)
			{
				if (std::find(mSelectedScenePaths.begin(), mSelectedScenePaths.end(), newPath) == mSelectedScenePaths.end())
				{
					mSelectedScenePaths.push_back(newPath);
				}
			}
		}
	}

	ImGui::SameLine();
	
	bool hasFiles = !mSelectedScenePaths.empty();
	if (!hasFiles)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
	}
	
	if (ImGui::Button("Clear All", ImVec2(buttonWidth, 40)) && hasFiles)
	{
		mSelectedScenePaths.clear();
	}
	
	if (!hasFiles)
	{
		ImGui::PopStyleVar();
	}
	
	ImGui::SameLine();
	
	if (!hasFiles)
	{
		ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.5f);
	}
	
	std::string actionButtonLabel = mIsExtensionMode ? "Add Selected" : "Load Selected";
	if (ImGui::Button(actionButtonLabel.c_str(), ImVec2(buttonWidth, 40)) && hasFiles)
	{
		if (mIsExtensionMode)
		{
			// Add as extension - don't unload current scene
			if (mAddExtensionScenesCallback)
			{
				mAddExtensionScenesCallback(mSelectedScenePaths);
			}
		}
		else
		{
			// Replace - unload current scene first
			if (mUnloadSceneCallback)
				mUnloadSceneCallback();
			
			if (mLoadMultipleScenesCallback)
			{
				mLoadMultipleScenesCallback(mSelectedScenePaths);
			}
		}
		mShowMultiSceneSelectionWindow = false;
	}
	
	if (!hasFiles)
	{
		ImGui::PopStyleVar();
	}

	ImGui::End();
}

void UIManager::RenderWarningWindow()
{
	ImVec2 displaySize = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos(ImVec2(displaySize.x * 0.5f, displaySize.y * 0.5f), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
	ImGui::SetNextWindowSize(ImVec2(500, 200), ImGuiCond_Appearing);

	if (!ImGui::Begin("Warning", &mShowWarning, ImGuiWindowFlags_NoCollapse))
	{
		ImGui::End();
		return;
	}

	ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.8f, 0.0f, 1.0f));
	ImGui::Text("Warning");
	ImGui::PopStyleColor();
	
	ImGui::Separator();
	ImGui::Spacing();

	ImGui::TextWrapped("%s", mWarningMessage.c_str());
	
	ImGui::Spacing();
	ImGui::Separator();
	ImGui::Spacing();

	float buttonWidth = 120.0f;
	float windowWidth = ImGui::GetWindowContentRegionMax().x - ImGui::GetWindowContentRegionMin().x;
	ImGui::SetCursorPosX((windowWidth - buttonWidth) * 0.5f + ImGui::GetWindowContentRegionMin().x);
	
	if (ImGui::Button("OK", ImVec2(buttonWidth, 40)))
	{
		mShowWarning = false;
		mWarningMessage.clear();
	}

	ImGui::End();
}

bool UIManager::OpenFileDialog(std::string& outPath)
{
	OPENFILENAMEA ofn{};
	char szFile[260] = { 0 };

	std::filesystem::path currentPath = std::filesystem::current_path();
	std::filesystem::path resourcesPath = currentPath / ".." / "Resources" / "Objects";

	std::string initialDir = std::filesystem::absolute(resourcesPath).string();

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = mHwnd;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = "3D Model Files\0*.gltf;*.glb;*.obj;*.fbx\0GLTF Files\0*.gltf;*.glb\0OBJ Files\0*.obj\0FBX Files\0*.fbx\0All Files\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = initialDir.c_str();
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

	if (GetOpenFileNameA(&ofn) == TRUE)
	{
		outPath = szFile;
		return true;
	}
	return false;
}

bool UIManager::OpenMultiFileDialog(std::vector<std::string>& outPaths)
{
	OPENFILENAMEA ofn{};
	const int bufferSize = 65536; // 64KB buffer for multiple files
	std::vector<char> szFile(bufferSize, 0);

	std::filesystem::path currentPath = std::filesystem::current_path();
	std::filesystem::path resourcesPath = currentPath / ".." / "Resources" / "Objects";

	std::string initialDir = std::filesystem::absolute(resourcesPath).string();

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = mHwnd;
	ofn.lpstrFile = szFile.data();
	ofn.nMaxFile = bufferSize;
	ofn.lpstrFilter = "3D Model Files\0*.gltf;*.glb;*.obj;*.fbx\0GLTF Files\0*.gltf;*.glb\0OBJ Files\0*.obj\0FBX Files\0*.fbx\0All Files\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = initialDir.c_str();
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

	if (GetOpenFileNameA(&ofn) == TRUE)
	{
		// Parse the multi-select buffer
		std::string directory = szFile.data();
		const char* pFile = szFile.data() + directory.length() + 1;

		// If there's only one file, the buffer contains just the full path
		if (*pFile == '\0')
		{
			outPaths.push_back(directory);
		}
		else
		{
			// Multiple files: first string is directory, rest are filenames
			while (*pFile != '\0')
			{
				std::string filename = pFile;
				std::filesystem::path fullPath = std::filesystem::path(directory) / filename;
				outPaths.push_back(fullPath.string());
				pFile += filename.length() + 1;
			}
		}
		return true;
	}
	return false;
}
