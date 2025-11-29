#pragma once

#include "D3D12/Command/D3D12CommandList.h"
#include "D3D12/Command/D3D12CommandQueue.h"
#include "D3D12/D3D12Device.h"
#include "D3D12/D3D12PipelineState.h"
#include "D3D12/D3D12Resource.h"
#include "ResourceLoading/UploadHeap.h"
#include "Camera/Camera.h"
#include "DataTypes.h"
#include "Depth/DepthBuffer.h"
#include "DXGI/DXGISwapChain.h"
#include "ResourceLoading/Model.h"
#include "ResourceLoading/TextureLoader.h"
#include "RenderAPI/Descriptors/ShaderVisibleDescriptorHeap.h"
#include "RenderAPI/HLSL/HLSLCompiler.h"
#include <unordered_map>

/**
 * @brief State of a GPU texture load operation, including async decode future.
 */
struct GpuTextureLoadState
{
	std::future<DecodedImage> decodeFuture;
	DecodedImage decodedImage;
	GPUTexture gpuTexture;
};

/**
 * @brief GPU resources for a single mesh (vertex/index buffers + material descriptor table).
 */
struct MeshGpuData
{
	D3D12Resource vb; ///< Vertex buffer resource.
	D3D12_VERTEX_BUFFER_VIEW vbv{}; ///< Vertex buffer view used for IA binding.
	D3D12Resource ib; ///< Index buffer resource.
	D3D12_INDEX_BUFFER_VIEW ibv{}; ///< Index buffer view used for IA binding.
	DescriptorAllocation materialTable; ///< Contiguous descriptors for material textures (t0 - t4).
};

/**
 * @brief High level renderer that wires up D3D12 device, swap chain, pipeline, and per-frame resources.
 */
class Renderer
{
private:
	D3D12Device mDevice; ///< Logical D3D12 device wrapper.
	DXGISwapChain mSwapChain; ///< Swap chain with back buffers.
	D3D12CommandList mCommandList; ///< Graphics command list and per-frame allocators.
	D3D12PipelineState mPipelineState; ///< Pipeline state and root signature.

	UINT mWidth = 0; ///< Back buffer width.
	UINT mHeight = 0; ///< Back buffer height.

	DepthBuffer mDepthBuffer; ///< Depth-stencil buffer and view.

	D3D12_VIEWPORT mViewport{}; ///< Viewport for rendering.
	D3D12_RECT mScissorRect{}; ///< Scissor rectangle.

	ConstantBufferData mConstantBufferData{}; ///< CPU-side constant buffer data (view-projection matrix).
	D3D12Resource mConstantBuffer; ///< GPU upload heap constant buffer.

	ShaderVisibleDescriptorHeap mSrvHeap; ///< Global shader-visible SRV heap for textures.
	TextureLoader mTextureLoader; ///< CPU/GPU texture loading helper.
	UploadHeap mUploadHeap; ///< Shared linear upload heap for staging data.

	std::unordered_map<std::string, GpuTextureLoadState> mTextureCache; ///< Cache of loaded GPU textures by path.

	std::vector<std::unique_ptr<Model>> mModels; ///< Loaded models.
	std::vector<MeshGpuData> mMeshGpu; ///< Flattened GPU data per mesh across all models.

	std::vector<LightData> mStaticLights; ///< Static lights loaded from models.

	HLSLCompiler mShaderCompiler; ///< HLSL shader compiler instance.

	D3D12CommandQueue mCommandQueue; ///< Command queue and fence synchronization (destroyed last).

	// timing
	LARGE_INTEGER mPrevCounter{};
	double mSecondsPerCount = 0.0;

	/**
	 * @brief Loads a texture from disk or returns cached GPU texture.
	 * @param path Absolute or relative texture file path.
	 * @param executeQueue Function to execute the command queue when needed.
	 * @return GPU texture wrapper with resource and SRV descriptor.
	 */
	GPUTexture LoadOrGetTexture(const std::string& path, const std::function<void()>& executeQueue);
	/**
	 * @brief Builds GPU buffers and material descriptor tables for all loaded meshes.
	 */
	void BuildMeshGpuData();


	/**
	 * @brief Collects static lights from loaded models into mStaticLights.
	 */
	void CollectStaticLights();

	/**
	 * @brief Creates a shader resource view for a texture resource.
	 * @param resource Texture resource.
	 * @param format Texture format.
	 * @param handle CPU descriptor handle where to create the SRV.
	 * @param mipLevels Number of mip levels in the texture.
	 */
	void CreateTextureView(ID3D12Resource* resource, DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE handle, UINT mipLevels);


	/**
	 * @brief Initializes or re-initializes the graphics pipeline state and root signature.
	 */
	void InitializePipelineState();

	/**
	 * @brief Initializes the texture loader with required D3D12 resources.
	 */
	void InitializeTextureLoader();
public:
	/**
	 * @brief Creates device/swap chain and initializes resources.
	 * @param hwnd Window handle.
	 * @param width Client width.
	 * @param height Client height.
	 */
	void Initialize(HWND hwnd, UINT width, UINT height);
	/**
	 * @brief Records and submits commands for one frame and presents.
	 * @param viewProj View-projection matrix dostarczony z zewn¹trz (CameraManager).
	 * @param cameraPos Camera world position.
	 * @param cameraForward Camera forward vector.
	 */
	void Update(const DirectX::XMMATRIX& viewProj, const DirectX::XMFLOAT3& cameraPos, const DirectX::XMFLOAT3& cameraForward);
};

