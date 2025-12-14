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
#include "nrd/NRD.h"

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
	DescriptorAllocation materialTable; ///< Contiguous descriptors for material textures (t0 - t4).

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

	D3D12PipelineState mPipelineStateOpaqueSingle; ///< Pipeline state and root signature for opaque single-sided objects.
	D3D12PipelineState mPipelineStateMaskedSingle; ///< Pipeline state and root signature for masked single-sided objects.
	D3D12PipelineState mPipelineStateTransparent; ///< Pipeline state and root signature for transparent objects.

	D3D12PipelineState mPipelineStateOpaqueDouble; ///< Pipeline state and root signature for opaque double-sided objects.
	D3D12PipelineState mPipelineStateMaskedDouble; ///< Pipeline state and root signature for masked double-sided objects.

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
	std::vector<MeshGpuData> mMaskedSingleSidedMeshes; ///< Flattened array of masked single-sided mesh GPU data for rendering.
	std::vector<MeshGpuData> mTransparentMeshes; ///< Flattened array of transparent mesh GPU data for rendering.

	std::vector<MeshGpuData> mOpaqueDoubleSidedMeshes; ///< Flattened array of opaque double-sided mesh GPU data for rendering.
	std::vector<MeshGpuData> mMaskedDoubleSidedMeshes; ///< Flattened array of masked double-sided mesh GPU data for rendering.

	std::vector<LightData> mStaticLights; ///< Static lights loaded from models.

	// RAY TRACING
	RayTracingBuilder mRtBuilder; ///< Ray tracing acceleration structure builder.
	D3D12Resource mTLAS;	///< Top-level acceleration structure result.
	D3D12Resource mTLAS_Scratch;	///< Top-level acceleration structure scratch buffer. May be used during updating, when objects move.
	D3D12Resource mInstanceDescBuffer;	///< Instance descriptions buffer for TLAS. TBH I don't know if it should be kept around after build.

	Microsoft::WRL::ComPtr<ID3D12StateObject> mRtStateObject; ///< Ray tracing state object (ray tracing pipeline).
	Microsoft::WRL::ComPtr<ID3D12RootSignature> mRtLocalRootSignature; ///< Ray tracing local root signature.
	Microsoft::WRL::ComPtr<ID3D12RootSignature> mRtGlobalRootSignature; ///< Ray tracing global root signature.

	//D3D12Resource mRtOutputResource; ///< Ray tracing output texture resource.
	//D3D12_CPU_DESCRIPTOR_HANDLE mRtOutputUavCpuHandle{}; ///< UAV descriptor handle for ray tracing output.
	//D3D12_GPU_DESCRIPTOR_HANDLE mRtOutputUavGpuHandle{}; ///< GPU handle for ray tracing output UAV.

	D3D12Resource mSbtResource; ///< Shader binding table resource.
	UINT64 mSbtEntrySize = 0; ///< Size of a single SBT entry (aligned).

	D3D12Resource mRtConstantBuffer; ///< Ray tracing constant buffer resource.
	RayGenConstantBuffer mRtConstantBufferData{}; ///< Ray-gen constant buffer data.
	UINT mRtConstantBufferStride = 0; ///< Size of a single ray-gen CB slice (bytes).
	D3D12Resource mMaterialBuffer; ///< Material buffer resource for ray tracing.


	bool mRayTracingEnabled = false; ///< Whether ray tracing is enabled.
	UINT mFrameCount = 0; ///< Frame count since start (for accumulation).



	void CreateRayTracingOutput();
	void CreateRayTracingPipeline();
	void CreateShaderBindingTable();
	void RenderRayTracing(const DirectX::XMMATRIX& viewProj, const DirectX::XMFLOAT3& camPos, const DirectX::XMFLOAT3& camFwd);
	/**
	 * @brief Initializes ray tracing acceleration structures.
	 */
	void InitializeRayTracing();
	// END RAY TRACING

	// DENOISING
	D3D12Resource mHistoryTexture; ///< History texture for accumulation.
	D3D12Resource mDenoiseOutput; ///< Noisy texture for accumulation.

	D3D12RootSignature mDenoiseRootSignature; ///< Denoising root signature.
	D3D12PipelineState mDenoisePipelineState; ///< Denoising pipeline state.

	DescriptorAllocation mDenoiseDescriptorTable; ///< Denoising descriptor table (input/output textures).

	void InitializeDenoising();
	void CreateDenoisePipeline();

	DirectX::XMFLOAT3 mPrevCameraPos{}; ///< Previous frame camera position (for accumulation reset).
	DirectX::XMFLOAT3 mPrevCameraForward{}; ///< Previous frame camera forward vector (for accumulation reset).
	// END DENOISING

	// MOTION VECTORS
	GPUTexture mMotionVectorTexture; ///< Motion vector texture.
	D3D12PipelineState mMotionVectorPipelineState; ///< Motion vector pipeline state.

	D3D12DescriptorHeap mRtvHeap; ///< RTV heap for motion vector texture.
	D3D12_CPU_DESCRIPTOR_HANDLE mMotionVectorRtvHandle{}; ///< RTV handle for motion vector texture.

	DirectX::XMMATRIX mPrevViewProj{}; ///< Previous frame view-projection matrix (for motion vector calculation).

	void InitializeMotionVectors();
	void RenderMotionVectors();

	// END MOTION VECTORS

	// NRD
	GPUTexture mNormalRoughnessTex;
	GPUTexture mViewZTex;

	nrd::Instance* mNrdInstance = nullptr;
	nrd::Denoiser* mNrdDenoiser = nullptr;
	std::vector<Microsoft::WRL::ComPtr<ID3D12PipelineState>> mNrdPipelines;
	std::vector<Microsoft::WRL::ComPtr<ID3D12RootSignature>> mNrdRootSignatures;

	D3D12_CPU_DESCRIPTOR_HANDLE mMotionVectorSrvCpuHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE mNormalRoughnessSrvCpuHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE mViewZSrvCpuHandle;
	//D3D12_CPU_DESCRIPTOR_HANDLE mRtOutputSrvCpuHandle; // Noisy Input
	//D3D12_CPU_DESCRIPTOR_HANDLE mDenoiseOutputUavCpuHandle; // Final Output

	DescriptorHeap mCpuHeap;
	DescriptorHeap mFrameHeap;

	uint32_t mNrdFrameIndex = 0;

	// Transient Texture Pool (Required by NRD)
	struct NrdPoolEntry
	{
		GPUTexture texture;
		D3D12_CPU_DESCRIPTOR_HANDLE srvHandle;
		D3D12_CPU_DESCRIPTOR_HANDLE uavHandle;
	};

	// Storage for the pools
	std::vector<NrdPoolEntry> mPermanentPool; // Persists across frames (History)
	std::vector<NrdPoolEntry> mTransientPool; // Reused/Discarded every frame

	void CreateNrdRootSignature(const nrd::PipelineDesc& pipeDesc, uint32_t index);
	void InitializeNRD();
	void DenoiseWithNRD(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj, const DirectX::XMFLOAT3& camPos);
	NrdPoolEntry& GetNrdPoolEntry(size_t index, nrd::ResourceType type);
	// END NRD

	// Demodulation
	D3D12Resource mAlbedoTex;

	// Resources for Outputs
	GPUTexture mRtDiffuseTex; 
	GPUTexture mRtSpecularTex; 
	GPUTexture mDenoisedDiffuseTex;    // (UAV output from NRD)
	GPUTexture mDenoisedSpecularTex;   // (UAV output from NRD)

	D3D12Resource mFinalColorOutput;   // Final composited output

	// Descriptors
	D3D12_GPU_DESCRIPTOR_HANDLE mRtDiffuseUavGpuHandle;

	D3D12_CPU_DESCRIPTOR_HANDLE mRtDiffuseUavCpuHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE mRtDiffuseSrvCpuHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE mRtSpecularUavCpuHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE mRtSpecularSrvCpuHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE mDenoisedDiffuseUavCpuHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE mDenoisedSpecularUavCpuHandle;

	D3D12_CPU_DESCRIPTOR_HANDLE mDenoisedDiffuseSrvCpuHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE mDenoisedSpecularSrvCpuHandle;

	D3D12_CPU_DESCRIPTOR_HANDLE mAlbedoSrvCpuHandle;
	D3D12_CPU_DESCRIPTOR_HANDLE mSpecularSrvCpuHandle;

	D3D12PipelineState mCompositePipelineState;
	void InitializeCompositePipeLineState();
	void DispatchComposite();

	void CreateUAV(ID3D12Resource* pResource, DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE handle);
	/// END Demodulation

	HLSLCompiler mShaderCompiler; ///< HLSL shader compiler instance.

	D3D12CommandQueue mCommandQueue; ///< Command queue and fence synchronization (destroyed last).

	// timing
	LARGE_INTEGER mPrevCounter{};
	double mSecondsPerCount = 0.0;


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
	void Update(const DirectX::XMMATRIX & view, const DirectX::XMMATRIX & proj, const DirectX::XMFLOAT3& cameraPos, const DirectX::XMFLOAT3& cameraForward);
};

