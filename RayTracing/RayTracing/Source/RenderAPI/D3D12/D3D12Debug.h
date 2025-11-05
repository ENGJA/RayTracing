#pragma once

/**
 * @brief Singleton wrapper for D3D12 debug layer interface.
 */
class D3D12Debug
{
private:
	Microsoft::WRL::ComPtr<ID3D12Debug6> mDebug;
	static D3D12Debug mInstance;

	D3D12Debug() = default;
	/**
	 * @brief Ensures the debug layer interface is initialized.
	 * @return true on success, false otherwise.
	 */
	bool EnsureInitialized();
	/**
	 * @brief Initializes the debug layer interface.
	 * @return true on success, false otherwise.
	 */
	bool Initialize();
public:
	/**
	 * @brief Returns the singleton instance.
	 */
	static D3D12Debug& GetInstance() { return mInstance; }
	/**
	 * @brief Enables the D3D12 debug layer.
	 */
	void Enable();
};