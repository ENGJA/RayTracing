#pragma once
#include "ResourceLoading/Mipmapping/MipmapPipeineState.h"
#include "RenderAPI/D3D12/Command/D3D12CommandList.h"
#include "RenderAPI/D3D12/Command/D3D12CommandQueue.h"
#include "RenderAPI/D3D12/D3D12DescriptorHeap.h"

class MipmapGenerator
{
	MipmapPipeineState mPipelineState;

	ID3D12Device* mDevice;					///< D3D12 device pointer
	D3D12CommandList* mCommandList;			///< Command list for recording commands
	D3D12CommandQueue* mCommandQueue;		///< Command queue for executing commands
	D3D12DescriptorHeap mDescriptorHeap;	///< Descriptor heap for SRV/UAV
	UINT mDescriptorSize;					///< Descriptor handle increment size

	void InitializeDescriptorHeap();

	static constexpr UINT cMaxHeapSize = 2048;
	UINT mCurrentDescriptorOffset = 0;

public:
	/**
	 * @brief Initializes the mipmap generator with required D3D12 resources.
	 * @param pDevice D3D12 device.
	 * @param computeShader Compute shader for mipmap generation.
	 * @param commandList Command list for recording commands.
	 * @param commandQueue Command queue for executing commands.
	 */
	void Initialize(ID3D12Device* pDevice, HLSLShader computeShader, D3D12CommandList* commandList, D3D12CommandQueue* commandQueue);

	/**
	 * @brief Generates mipmaps for the given texture resource.
	 * @param textureResource Texture resource with mip levels to generate.
	 * @param width Width of the top mip level.
	 * @param height Height of the top mip level.
	 * @param mipLevels Total number of mip levels in the texture.
	 * @param frameIndex Current frame index for command list recording.
	 * @param executeQueue Function to execute the command queue when needed.
	 */
	void GenerateMipmaps(ID3D12Resource* textureResource, UINT width, UINT height, UINT mipLevels, DXGI_FORMAT format, UINT frameIndex, const std::function<void()>& executeQueue);

	/**
	 * @brief Resets internal state, such as the descriptor offset.
	 */
	void Reset() { mCurrentDescriptorOffset = 0; }
};

