#pragma once
#include "config.h"

/**
 * @brief Thin wrapper around an ID3D12GraphicsCommandList and per-frame allocators.
 */
class D3D12CommandList
{
private:
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> mCommandList; ///< Graphics command list.
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mCommandAllocators[Config::cBufferCount]; ///< Per-frame command allocators.
public:
	/**
	 * @brief Creates the command list and allocators.
	 * @param pDevice D3D12 device.
	 */
	void Initialize(ID3D12Device* pDevice);

	/**
	 * @brief Returns the native command list pointer.
	 */
	ID3D12GraphicsCommandList* Get() const { return mCommandList.Get(); }

	/**
	 * @brief Resets the command allocator and command list for the given frame index.
	 * @param frameIndex Index into the back buffer/allocator ring [0..Config::cBufferCount).
	 */
	void ResetCommandList(UINT frameIndex);
};

