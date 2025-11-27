#pragma once
#include "RenderAPI/D3D12/D3D12Resource.h"
#include "RenderAPI/Descriptors/ShaderVisibleDescriptorHeap.h"
#include "RenderAPI/D3D12/Command/D3D12CommandQueue.h"
#include "RenderAPI/D3D12/Command/D3D12CommandList.h"
#include "ResourceLoading/UploadHeap.h"
#include "Mipmapping/MipmapGenerator.h"

struct GPUTexture
{
    D3D12Resource resource;
	DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
	UINT mipLevels = 0;
    UINT width = 0;
    UINT height = 0;
};

struct DecodedImage
{
    UINT width{};
    UINT height{};
    std::vector<BYTE> pixels; // RGBA8
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
	* @brief Loads a 2D texture from file and uploads it to GPU.
	* @param path File path to the image.
	* @param frameIndex Current frame index for command list recording.
	* @param executeQueue Function to execute the command queue when needed.
	* @return Uploaded GPU texture with resource and SRV.
    */
	GPUTexture LoadTexture2DFromFile(const std::wstring& path, UINT frameIndex, const std::function<void()>& executeQueue);

	/**
	* @brief Resets internal state, such as the mipmap generator.
	*/
	void Reset() { mMipmapGenerator.Reset(); }

	/**
	 * @brief Ensure that 1x1 black fallback texture is created (lazily).
	 * @param frameIndex Current frame index used for mipmap generation.
	 * @param executeQueue Callback to flush command list / upload heap when needed.
	 */
	void EnsureFallbackTexture(UINT frameIndex, const std::function<void()>& executeQueue);

	/**
	 * @brief Returns the fallback GPU texture (1x1 black). Call EnsureFallbackTexture() first.
	 */
	GPUTexture GetFallbackTexture() const { return mFallbackTexture; }

private:
    /** <WIC imaging factory for image decoding. */
    Microsoft::WRL::ComPtr<IWICImagingFactory> mWIC;

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

	/** Fallback 1x1 black texture returned when material lacks emissive / other maps. */
	GPUTexture mFallbackTexture;

    /**
	* @brief Decodes an image file into RGBA8 pixel data.
	* @param path File path to the image.
	* @return Decoded image with width, height, and pixel data.
    */
	DecodedImage DecodeImageRGBA8(const std::wstring& path);

	/**
	* @brief Creates a GPU texture from decoded image data.
	* @param img Decoded image data.
	* @param frameIndex Current frame index for command list recording.
	* @param executeQueue Function to execute the command queue when needed.
	* @return Created GPU texture with resource and SRV.
	*/
	GPUTexture CreateTextureFromDecodedImage(const DecodedImage& img, UINT frameIndex, const std::function<void()>& executeQueue);


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
