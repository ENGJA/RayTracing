#pragma once
#include "RenderAPI/Renderer.h"



class Application
{
private:
	Renderer mRenderer;

	HWND mHwnd = nullptr;
	bool mIsRunning = true;
	UINT mWidth = 0;
	UINT mHeight = 0;


public:
	bool IsRunning() const { return mIsRunning; }
	bool Initialize(LPCWSTR className, LPCWSTR windowName, int width = 1280, int height = 720);
	void Update();

	void OnCreate(HWND hwnd);
	void OnDestroy();
};

