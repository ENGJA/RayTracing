#pragma once
#include "RenderAPI/D3D12/D3D12Resource.h"
#include "RenderAPI/Descriptors/ShaderVisibleDescriptorHeap.h"
#include "RenderAPI/D3D12/Command/D3D12CommandQueue.h"
#include "RenderAPI/D3D12/Command/D3D12CommandList.h"
#include "ResourceLoading/UploadHeap.h"
#include "Mipmapping/MipmapGenerator.h"
#include "ResourceLoading/ImageDecoder.h"

struct GPUTexture
{
    D3D12Resource resource;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	UINT mipLevels = 0;
    UINT width = 0;
    UINT height = 0;
};


/**
 * @brief Texture loader that decodes image files and uploads them to GPU textures.
*/
class TextureLoader
{
public:
    /**
	* @brief Initializes the texture loader with required D3D12 resources.
	* @param device D3D12 device.
	* @param heap Shader-visible descriptor heap for SRV allocation.
	* @param queue Command queue for copy execution.
	* @param cmdList Command list for recording copy commands.
	* @param uploadHeap Upload heap for staging texture data.    
    */
	void Initialize(ID3D12Device* device, ShaderVisibleDescriptorHeap* heap, D3D12CommandQueue* queue, D3D12CommandList* cmdList, UploadHeap* uploadHeap, HLSLShader mipmapComputeShader);

	/**
	* @brief Creates a GPU texture from decoded image data.
	* @param img Decoded image data.
	* @param frameIndex Current frame index for command list recording.
	* @param executeQueue Function to execute the command queue when needed.
	* @return Created GPU texture with resource and SRV.
	*/
	GPUTexture CreateTextureFromDecodedImage(const DecodedImage& img, UINT frameIndex, const std::function<void()>& executeQueue);

	/**
	* @brief Resets internal state, such as the mipmap generator.
	*/
	void Reset() { mMipmapGenerator.Reset(); }

private:
	/** <D3D12 device pointer. */
    ID3D12Device* mDevice = nullptr;

	/** <Shader-visible descriptor heap for SRV allocation. */
    ShaderVisibleDescriptorHeap* mHeap = nullptr;

	/** <Command queue for copy execution. */
    D3D12CommandQueue* mQueue = nullptr;

	/** <Command list for recording copy commands. */
    D3D12CommandList* mCmdList = nullptr;

	/** <Upload heap for staging texture data. */
    UploadHeap* mUploadHeap = nullptr; // shared staging heap

	MipmapGenerator mMipmapGenerator; ///< Mipmap generator for generating mipmaps on GPU.


	/**
	* @brief Creates a 2D texture resource description.
	* @param width Texture width.
	* @param height Texture height.
	*/
	static D3D12_RESOURCE_DESC CreateTexture2DDesc(UINT width, UINT height, UINT16 mipLevels);

	/**
	* @brief Creates a default transition barrier for a texture resource (COPY_DEST to PIXEL_SHADER_RESOURCE).
	* @param resource Texture resource.
	* @return Resource barrier structure.
	*/
	static D3D12_RESOURCE_BARRIER CreateTextureTransitionBarrier(ID3D12Resource* resource);
};
