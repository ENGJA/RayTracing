#pragma once

/*
* @brief Singleton wrapper for DXGI debug interface.
*/
class DXGIDebug
{
private:
	Microsoft::WRL::ComPtr<IDXGIDebug1> mDebug;
	static DXGIDebug mInstance;

	DXGIDebug() = default;
	/**
	 * @brief Ensures the debug interface is initialized.
	 * @return true on success, false otherwise.
	 */
	bool EnsureInitialized();
	/**
	 * @brief Initializes the debug interface.
	 * @return true on success, false otherwise.
	 */
	bool Initialize();
public:
	/**
	 * @brief Returns the singleton instance.
	 */
	static DXGIDebug& GetInstance() { return mInstance; }
	/**
	 * @brief Enables the DXGI debug layer.
	 */
	void Enable();
	/**
	 * @brief Reports live DXGI objects to the debug output.
	 */
	void ReportLiveObjects();
};
