#pragma once
#include "DXGIAdapter.h"

/**
 * @brief Factory wrapper for enumerating adapters and creating DXGI objects.
 */
class DXGIFactory
{
private:
	Microsoft::WRL::ComPtr<IDXGIFactory7> mFactory;
public:
	/**
	 * @brief Creates a DXGI factory instance.
	 */
	DXGIFactory();
	/**
	 * @brief Returns the first suitable hardware adapter.
	 */
	DXGIAdapter GetAdapter();
	/**
	 * @brief Returns the native factory pointer.
	 */
	IDXGIFactory7* Get() { return mFactory.Get(); }
};

