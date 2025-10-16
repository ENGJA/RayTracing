#pragma once

class DXGIDebug : public Microsoft::WRL::ComPtr<IDXGIDebug1>
{
private:
	static DXGIDebug mInstance;

	DXGIDebug() = default;
	bool EnsureInitialized();
	bool Initialize();
public:
	static DXGIDebug& GetInstance() { return mInstance; }
	void Enable();
	void ReportLiveObjects();
};
