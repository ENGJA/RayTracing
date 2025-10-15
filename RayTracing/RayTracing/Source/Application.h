#pragma once
#include <Windows.h>



class Application
{
public:
	Application() = default;
	bool IsRunning() const { return mIsRunning; }
	bool Initialize(LPCWSTR className, LPCWSTR windowName, int width = 1280, int height = 720);
	//void Update();

private:
	HWND mHwnd = nullptr;
	bool mIsRunning = true;
};

