#pragma once
#include "RenderAPI/Renderer.h"



class Application
{
private:
	Renderer mRenderer;

	HWND mHwnd = nullptr;
	bool mIsRunning = true;

public:
	bool IsRunning() const { return mIsRunning; }
	bool Initialize(LPCWSTR className, LPCWSTR windowName, int width = 1280, int height = 720);
	void Update();

	void OnCreate();
	void OnDestroy();
};

