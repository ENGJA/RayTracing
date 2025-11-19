#pragma once
#include "DepthDescHeap.h"
#include "RenderAPI/D3D12/D3D12Resource.h"

/**
 * @brief Depth-stencil buffer and descriptor heap owner.
 */
class DepthBuffer
{
private:
	D3D12Resource mDepthStencilBuffer; ///< Depth-stencil texture resource.
	DepthDescHeap mDescHeap; ///< Descriptor heap containing one DSV.

public:
	/**
	 * @brief Creates the depth buffer resource and its DSV.
	 * @param pDevice D3D12 device.
	 * @param width Buffer width.
	 * @param height Buffer height.
	 */
	void Initialize(ID3D12Device* pDevice, UINT width, UINT height);
	/**
	 * @brief Gets the native depth resource pointer.
	 */
	ID3D12Resource* GetResource() const { return mDepthStencilBuffer.Get(); }
	/**
	 * @brief Returns the depth-stencil view CPU handle.
	 */
	D3D12_CPU_DESCRIPTOR_HANDLE GetDSVHandle() const { return mDescHeap.GetDSVHandle(); }
};

