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
	
	// Load Scene button at Y = 50
	ImGui::SetCursorPos(ImVec2(x, 50.0f));
	if (ImGui::Button("Load Scene from File...", ImVec2(370, 50)))
	{
		std::string scenePath;
		if (OpenFileDialog(scenePath) && mLoadSceneCallback)
		{
			mLoadSceneCallback(scenePath);
		}
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
	ImGui::SetNextWindowSize(ImVec2(400, 460), ImGuiCond_Always);

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
	
	// Button 4: Load Scene
	ImGui::SetCursorPos(ImVec2(buttonX, y));
	if (ImGui::Button("Load Different Scene...", ImVec2(buttonWidth, buttonHeight)))
	{
		std::string scenePath;
		if (OpenFileDialog(scenePath))
		{
			if (mUnloadSceneCallback)
				mUnloadSceneCallback();
			if (mLoadSceneCallback)
				mLoadSceneCallback(scenePath);
		}
	}
	y += buttonHeight + gap;
	
	// Button 5: Exit to Menu
	ImGui::SetCursorPos(ImVec2(buttonX, y));
	if (ImGui::Button("Exit to Main Menu", ImVec2(buttonWidth, buttonHeight)))
	{
		if (mExitToMainMenuCallback)
			mExitToMainMenuCallback();
	}
	y += buttonHeight + gap;
	
	// Button 6: Exit App
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
	ImGui::SetNextWindowSize(ImVec2(450, 180), ImGuiCond_Once);

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
	
	// Double the frame padding to make slider 2x thicker
	ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(8.0f, 16.0f));
	
	if (ImGui::SliderFloat("##speed", &currentSpeed, 0.1f, 5.0f, "%.1fx"))
	{
		mCameraManager->SetMoveSpeedMultiplier(currentSpeed);
	}
	
	ImGui::PopStyleVar();
	
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
		
		if (gpuUsage > 0.0f)
			ImGui::Text("GPU: %.1f%%", gpuUsage);
		else
			ImGui::TextDisabled("GPU: N/A");
		
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
	ImGui::SetNextWindowSize(ImVec2(400, 150), ImGuiCond_Always);

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
	ImGui::SetCursorPos(ImVec2(15.0f, 100.0f));
	static float loadingTime = 0.0f;
	loadingTime += ImGui::GetIO().DeltaTime;
	int dots = (int)(loadingTime * 2.0f) % 4;
	std::string loadingText = "Loading";
	for (int i = 0; i < dots; i++)
		loadingText += ".";
	ImGui::Text("%s", loadingText.c_str());

	ImGui::End();
}

bool UIManager::OpenFileDialog(std::string& outPath)
{
	OPENFILENAMEA ofn{};
	char szFile[260] = { 0 };

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = mHwnd;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = "3D Model Files\0*.gltf;*.glb;*.obj;*.fbx\0GLTF Files\0*.gltf;*.glb\0OBJ Files\0*.obj\0FBX Files\0*.fbx\0All Files\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

	if (GetOpenFileNameA(&ofn) == TRUE)
	{
		outPath = szFile;
		return true;
	}
	return false;
}
