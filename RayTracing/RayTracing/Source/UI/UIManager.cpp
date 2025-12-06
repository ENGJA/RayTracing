#include "pch.h"
#include "UIManager.h"
#include "RenderAPI/Camera/CameraManager.h"
#include "imgui.h"

void UIManager::Initialize(HWND hwnd, CameraManager* cameraManager)
{
	mHwnd = hwnd;
	mCameraManager = cameraManager;
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
	else // Scene mode
	{
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
	ImGui::SetNextWindowSize(ImVec2(400, 360), ImGuiCond_Always);

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
	
	// Button 3: Load Scene
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
			if (mToggleMenuCallback)
				mToggleMenuCallback();
		}
	}
	y += buttonHeight + gap;
	
	// Button 4: Exit to Menu
	ImGui::SetCursorPos(ImVec2(buttonX, y));
	if (ImGui::Button("Exit to Main Menu", ImVec2(buttonWidth, buttonHeight)))
	{
		if (mExitToMainMenuCallback)
			mExitToMainMenuCallback();
	}
	y += buttonHeight + gap;
	
	// Button 5: Exit App
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
	ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(450, 180), ImGuiCond_FirstUseEver);

	if (!ImGui::Begin("Settings", &mShowSettingsWindow, ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize))
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
	ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(500, 300), ImGuiCond_FirstUseEver);

	if (!ImGui::Begin("About 3D Scene Viewer", &mShowAboutWindow))
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
	ImGui::SetNextWindowPos(ImVec2(100, 100), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(500, 450), ImGuiCond_FirstUseEver);

	if (!ImGui::Begin("Controls", &mShowControlsWindow))
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
