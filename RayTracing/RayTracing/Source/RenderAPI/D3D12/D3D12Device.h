#pragma once

/**
 * @brief RAII wrapper for ID3D12Device5.
 */
class D3D12Device
{
private:
	Microsoft::WRL::ComPtr<ID3D12Device5> mDevice;

public:
	/**
	 * @brief Creates the logical D3D12 device from the given adapter.
	 * @param pAdapter DXGI adapter.
	 */
	void Initialize(IDXGIAdapter* pAdapter);
	/**
	 * @brief Returns the native device pointer.
	 */
	ID3D12Device5* Get() const { return mDevice.Get(); }
};

