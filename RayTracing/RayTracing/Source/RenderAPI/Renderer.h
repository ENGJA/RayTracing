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
#include "RenderAPI/Descriptors/DescriptorHeap.h"
#include "RenderAPI/HLSL/HLSLCompiler.h"
#include "RenderAPI/RT/RayTracingBuilder.h"
#include <unordered_map>

/**
 * @brief State of a GPU texture load operation, including async decode future.
 */
struct GPUTextureLoadState
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
	DescriptorHandle materialTable; ///< Contiguous descriptors for material textures (t0 - t4).

	MeshMaterialData materialData{}; ///< Material data for constant buffer upload.

	DirectX::XMFLOAT3 center; ///< Mesh bounding sphere center in model space.
	float distanceToCamera = 0.0f; ///< Distance from mesh center to camera (for sorting).

	D3D12Resource blasResult; ///< Bottom-level acceleration structure resource for ray tracing.
	//UINT blasIndex;
};

struct DefaultTextures
{
	GPUTexture white;
	//GPUTexture black;
	GPUTexture normal;
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

	D3D12RootSignature mMeshRootSignature; ///< Root signature for mesh rendering.
	D3D12RootSignature mComputeRootSignature; ///< Root signature for compute shader passes.


	D3D12PipelineState mPipelineStateOpaqueSingle; ///< Pipeline state and root signature for opaque single-sided objects.
	D3D12PipelineState mPipelineStateMaskedSingle; ///< Pipeline state and root signature for masked single-sided objects.
	D3D12PipelineState mPipelineStateTransparent; ///< Pipeline state and root signature for transparent objects.

	D3D12PipelineState mPipelineStateOpaqueDouble; ///< Pipeline state and root signature for opaque double-sided objects.
	D3D12PipelineState mPipelineStateMaskedDouble; ///< Pipeline state and root signature for masked double-sided objects.

	D3D12PipelineState mPipelineStateCompute; ///< Pipeline state for compute shader passes.

	UINT mWidth = 0; ///< Back buffer width.
	UINT mHeight = 0; ///< Back buffer height.

	DepthBuffer mDepthBuffer; ///< Depth-stencil buffer and view.

	D3D12_VIEWPORT mViewport{}; ///< Viewport for rendering.
	D3D12_RECT mScissorRect{}; ///< Scissor rectangle.

	ConstantBufferData mConstantBufferData{}; ///< CPU-side constant buffer data (view-projection matrix).
	D3D12Resource mConstantBuffer; ///< GPU upload heap constant buffer.

	DescriptorHeap mSrvHeap; ///< Global shader-visible SRV heap for textures.
	TextureLoader mTextureLoader; ///< CPU/GPU texture loading helper.
	UploadHeap mUploadHeap; ///< Shared linear upload heap for staging data.

	std::unordered_map<std::string, GPUTextureLoadState> mTextureCache; ///< Cache of loaded GPU textures by path.

	DefaultTextures mDefaultTextures; ///< Default white/black/normal textures.
	std::vector<std::unique_ptr<Model>> mModels; ///< Loaded models.
	std::vector<MeshGpuData> mOpaqueSingleSidedMeshes; ///< Flattened array of opaque single-sided mesh GPU data for rendering.
	std::vector<MeshGpuData> mMaskedSingleMeshes; ///< Flattened array of masked single-sided mesh GPU data for rendering.
	std::vector<MeshGpuData> mTransparentMeshes; ///< Flattened array of transparent mesh GPU data for rendering.

	std::vector<MeshGpuData> mOpaqueDoubleSidedMeshes; ///< Flattened array of opaque double-sided mesh GPU data for rendering.
	std::vector<MeshGpuData> mMaskedDoubleSidedMeshes; ///< Flattened array of masked double-sided mesh GPU data for rendering.

	std::vector<LightData> mStaticLights; ///< Static lights loaded from models.

	RayTracingBuilder mRayTracingBuilder; ///< Ray tracing acceleration structure builder.
	D3D12Resource mTLAS;	///< Top-level acceleration structure result.
	D3D12Resource mTLAS_Scratch;	///< Top-level acceleration structure scratch buffer. May be used during updating, when objects move.
	D3D12Resource mInstanceDescBuffer;	///< Instance descriptions buffer for TLAS. TBH I don't know if it should be kept around after build.



	D3D12Resource mGBufferAlbedo;    ///< G-buffer render target for albedo (RGBA8).
	D3D12Resource mGBufferNormal;    ///< G-buffer render target for normals (RGBA16F).
	D3D12Resource mGBufferMaterial;  ///< G-buffer render target for material properties (RGBA8).

	DescriptorHeap mGBufferRtvHeap; ///< RTV heap for G-buffer render targets.

	D3D12PipelineState mComputeState;; ///< Pipeline state for compute shader passes.
	//D3D12RootSignature mComputeRootSignature; ///< Root signature for compute shader passes.

	D3D12Resource mComputeOutputTexture; ///< Output texture for compute shader passes.
	D3D12Resource mGlobalLightBuffer; ///< Structured buffer for global lights.


	// --- Descriptor Indices (Saved during initialization) ---
	int mSrvSlot_GBufferAlbedo = -1;
	int mSrvSlot_GBufferNormal = -1;
	int mSrvSlot_GBufferMaterial = -1;
	int mSrvSlot_Depth = -1;

	int mUavSlot_Output = -1;       // For the Compute Shader Output
	int mSrvSlot_LightBuffer = -1;  // For the StructuredBuffer<Light>



	void InitializeGBufferResources();
	void InitializeComputePipeline();
	void CreateLightBuffer();


	HLSLCompiler mShaderCompiler; ///< HLSL shader compiler instance.

	D3D12CommandQueue mCommandQueue; ///< Command queue and fence synchronization (destroyed last).

	// timing
	LARGE_INTEGER mPrevCounter{};
	double mSecondsPerCount = 0.0;


	void InitializeRootSignatures();

	/**
	 * @brief Initializes or re-initializes the graphics pipeline state and root signature.
	 */
	void InitializePipelineState();

	/**
	 * @brief Initializes the texture loader with required D3D12 resources.
	 */
	void InitializeTextureLoader();

	/**
	* @brief Builds GPU resources for all loaded meshes in all models.
	*/
    void BuildMeshGpuData();


	/**
	* @brief Collects static lights from all models into a single array for efficient access.
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
	* @brief Dispatches asynchronous texture decoding tasks for all textures used in loaded models.
	*/
    void DispatchTextureDecoding();

	/**
	* @brief Uploads GPU resources for all meshes in all models.
	* @param executeBatch Function to execute the command queue when needed.
	*/
    void UploadMeshes(const std::function<void()>& executeBatch);

	/**
	* @brief Uploads GPU resources for a single mesh.
	* @param mesh Mesh to upload.
	* @param directory Directory of the model owning the mesh (for texture paths).
	* @param executeBatch Function to execute the command queue when needed.
	*/
	void UploadSingleMesh(const Mesh& mesh, const std::string& directory, const std::function<void()>& executeBatch);

	/**
	 * @brief Creates material texture descriptors for a mesh.
	 * @param directory Directory of the model owning the mesh (for texture paths).
	 * @param mesh Mesh whose material to create.
	 * @param dst CPU descriptor handle where to write the material SRV descriptors.
	 * @param executeBatch Function to execute the command queue when needed.
	 */
	void CreateMaterial(const Mesh& mesh, const std::string& directory, MeshGpuData& gpuData, const std::function<void()>& executeBatch);

	/**
	 * @brief Draws a single mesh (binds its buffers and material).
	 * @param mesh Mesh GPU data to draw.
	 */
	void DrawMesh(const MeshGpuData& mesh);

	/**
	 * @brief Sorts transparent meshes back-to-front based on camera position.
	 * @param cameraPos Camera world position.
	 */
	void SortTransparentMeshes(const DirectX::XMFLOAT3& cameraPos);

	/**
	 * @brief Initializes default dummy textures (white, normal).
	 */
	void InitializeDummyTextures();

	/**
	 * @brief Initializes ray tracing acceleration structures.
	 */
	void InitializeRayTracing();
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

