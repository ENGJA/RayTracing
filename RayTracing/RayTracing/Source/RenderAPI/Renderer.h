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
#include "RenderAPI/RT/RayTracingPipeline.h"
#include <unordered_map>


#include <streamline/sl.h>
#include <streamline/sl_dlss.h>
#include <streamline/sl_dlss_d.h>

struct AlignedConstantBufferData
{
	ConstantBufferData data;
	UINT8 padding[D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT - (sizeof(ConstantBufferData) % D3D12_CONSTANT_BUFFER_DATA_PLACEMENT_ALIGNMENT)]{}; // Padding to 256-byte alignment
};

// Forward declaration
struct ImGuiContext;

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

enum class RenderMode
{
	Hybrid,
	ForwardPhong,
	RayTraced,
};

/**
 * @brief High level renderer that wires up D3D12 device, swap chain, pipeline, and per-frame resources.
 */
class Renderer
{
private:
	D3D12Device mDevice; ///< Logical D3D12 device wrapper.
	Microsoft::WRL::ComPtr<IDXGIAdapter3> mAdapter; ///< DXGI adapter for VRAM queries.
	DXGISwapChain mSwapChain; ///< Swap chain with back buffers.
	D3D12CommandList mCommandList; ///< Graphics command list and per-frame allocators.

	bool mShadowsEnabled = true;
	bool mReflectionsEnabled = true;
	int mMaxReflectionDepth = 1; // Default
	int mMaxTransmissionDepth = 3;

	DirectX::XMFLOAT3 mSunDirection = { 0.2f, -1.0f, 0.2f };
	DirectX::XMFLOAT3 mSunColor = { 1.0f, 1.0f, 0.9f }; // Default sun color
	bool mSunEnabled = true;                            // Default state
	int mSunIndex = -1;                                 // Track sun's index in mStaticLights


	RenderMode mCurrentRenderMode = RenderMode::ForwardPhong;

	// Phong 
	D3D12Resource mPhongOutputTexture;
	DescriptorHandle mRtvHandle_PhongOutput;
	DescriptorHandle mSrvHandle_PhongOutput;

	D3D12PipelineState mPipelineStatePhongOpaqueSingle;
	D3D12PipelineState mPipelineStatePhongOpaqueDouble;
	D3D12PipelineState mPipelineStatePhongTransparentSingle;
	D3D12PipelineState mPipelineStatePhongTransparentDouble;
	D3D12PipelineState mPipelineStatePhongMaskedSingle;
	D3D12PipelineState mPipelineStatePhongMaskedDouble;
	void RenderPhong(const Camera& camera);


	// RT
	RayTracingPipeline mFullRTPipeline;
	D3D12RootSignature mFullRTGlobalRootSignature;
	void RenderFullRayTraced(const Camera& camera, size_t cbOffset);
	DescriptorHandle mUavHandle_GBufferNormal;
	DescriptorHandle mUavHandle_GBufferMaterial;
	DescriptorHandle mUavHandle_GBufferEmissive;

	D3D12Resource mRTDepthTexture;
	DescriptorHandle mUavHandle_RTDepth;
	UINT mRISCandidates = 8;
	UINT mPointShadowRays = 1;




	D3D12RootSignature mMeshRootSignature; ///< Root signature for mesh rendering.
	D3D12RootSignature mComputeRootSignature; ///< Root signature for compute shader passes.
	D3D12RootSignature mRtGlobalRootSignature; ///< Global root signature for ray tracing.


	D3D12PipelineState mPipelineStateOpaqueSingle; ///< Pipeline state and root signature for opaque single-sided objects.
	D3D12PipelineState mPipelineStateMaskedSingle; ///< Pipeline state and root signature for masked single-sided objects.
	D3D12PipelineState mPipelineStateTransparentSingle; ///< Pipeline state and root signature for transparent objects.

	D3D12PipelineState mPipelineStateOpaqueDouble; ///< Pipeline state and root signature for opaque double-sided objects.
	D3D12PipelineState mPipelineStateMaskedDouble; ///< Pipeline state and root signature for masked double-sided objects.
	D3D12PipelineState mPipelineStateTransparentDouble;

	D3D12PipelineState mPipelineStateCompute; ///< Pipeline state for compute shader passes.

	UINT mWidth = 0; ///< Back buffer width.
	UINT mHeight = 0; ///< Back buffer height.

	DepthBuffer mDepthBuffer; ///< Depth-stencil buffer and view. (DLSS resolution)
	DepthBuffer mNativeDepthBuffer; ///< Depth-stencil buffer and view. (Native resolution for Phong)


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
	std::vector<MeshGpuData> mTransparentSingleSidedMeshes; ///< Flattened array of transparent mesh GPU data for rendering.

	std::vector<MeshGpuData> mOpaqueDoubleSidedMeshes; ///< Flattened array of opaque double-sided mesh GPU data for rendering.
	std::vector<MeshGpuData> mMaskedDoubleSidedMeshes; ///< Flattened array of masked double-sided mesh GPU data for rendering.
	std::vector<MeshGpuData> mTransparentDoubleSidedMeshes;

	std::vector<LightData> mStaticLights; ///< Static lights loaded from models.

	RayTracingBuilder mRayTracingBuilder; ///< Ray tracing acceleration structure builder.
	D3D12Resource mTLAS;	///< Top-level acceleration structure result.
	D3D12Resource mTLAS_Scratch;	///< Top-level acceleration structure scratch buffer. May be used during updating, when objects move.
	D3D12Resource mInstanceDescBuffer;	///< Instance descriptions buffer for TLAS. TBH I don't know if it should be kept around after build.


	// ===========================================
	// G-buffer resources for deferred rendering

	D3D12Resource mGBufferAlbedo;    ///< G-buffer render target for albedo (RGBA8).
	D3D12Resource mGBufferNormal;    ///< G-buffer render target for normals (RGBA16F).
	D3D12Resource mGBufferMaterial;  ///< G-buffer render target for material properties (RGBA8).
	D3D12Resource mGBufferEmission;  ///< G-buffer render target for emission (RGBA16F).

	DescriptorHeap mRtvHeap; ///< RTV heap for G-buffer render targets.

	D3D12PipelineState mComputeState;; ///< Pipeline state for compute shader passes.
	//D3D12RootSignature mComputeRootSignature; ///< Root signature for compute shader passes.

	D3D12Resource mCompositeOutputTexture; ///< Output texture for compute shader passes.

	//D3D12Resource mDirectLightingTexture; ///< Intermediate texture for direct lighting results.

	D3D12Resource mGlobalLightBuffer; ///< Structured buffer for global lights.

	//D3D12Resource mOutDiffuseTex;
	//D3D12Resource mOutSpecularTex;


	// --- Descriptor Indices (Saved during initialization) ---
	DescriptorHandle mRtvHandle_GBufferAlbedo;
	DescriptorHandle mRtvHandle_GBufferNormal;
	DescriptorHandle mRtvHandle_GBufferMaterial;
	DescriptorHandle mRtvHandle_GBufferEmissive;
	DescriptorHandle mRtvHandle_GBufferVelocity;

	DescriptorHandle mSrvHandle_GBufferAlbedo;
	DescriptorHandle mSrvHandle_GBufferNormal;
	DescriptorHandle mSrvHandle_GBufferMaterial;
	DescriptorHandle mSrvHandle_GBufferEmissive;
	DescriptorHandle mSrvHandle_GBufferVelocity;
	DescriptorHandle mSrvHandle_Depth;

	DescriptorHandle mUavHandle_CompositeOutput;
	DescriptorHandle mRtvHandle_CompositeOutput;
	DescriptorHandle mSrvHandle_CompositeOutput;

	DescriptorHandle mSrvHandle_LightBuffer;  // For the StructuredBuffer<Light>
	//int mSrvSlot_LightBuffer = -1;  // For the StructuredBuffer<Light>


	void InitializeGBufferResources();
	void InitializeCompositeResources();
	void CreateLightBuffer();

	// ===========================================

	// ===========================================
	// Ray tracing pipeline for reflections

	RayTracingPipeline mReflectionsPipeline;

	//DescriptorHandle mUavHandle_Diffuse;
	//DescriptorHandle mUavHandle_Specular;
	//DescriptorHandle mSrvHandle_Diffuse;
	//DescriptorHandle mSrvHandle_Specular;

	D3D12RootSignature mCompositeRootSignature;
	D3D12PipelineState mPipelineStateComposite;

	void InitializeReflectionResources();

	// ===========================================

	// ===========================================
	// DLSS Integration
	D3D12Resource mDLSSOutputTexture;
	DescriptorHandle mUavHandle_DlssOutput;
	DescriptorHandle mSrvHandle_DlssOutput;
	DescriptorHandle mRtvHandle_DlssOutput;
	//int mUavSlot_DlssOutput = -1;
	//int mSrvSlot_DlssOutput = -1;

	DirectX::XMMATRIX mPrevViewMatrix;
	DirectX::XMMATRIX mPrevProjMatrix;
	sl::DLSSMode mDLSSMode = sl::DLSSMode::eMaxQuality;

	sl::Feature* mDLSS_FeatureHandle = nullptr;
	sl::DLSSOptions mDLSS_Options{};
	sl::DLSSDOptions mDLSSD_Options{};
	sl::Constants mDLSS_Constants{};

	D3D12Resource mGBufferVelocity;    ///< G-buffer render target for motion vectors (RG16F).

	sl::ViewportHandle mSlViewport = { 0 };
	uint32_t mRenderWidth = 0;
	uint32_t mRenderHeight = 0;

	DirectX::XMFLOAT2 mJitter = { 0.0f, 0.0f };
	DirectX::XMFLOAT2 mJitterPixels = { 0.0f, 0.0f };

	bool mDLSSRREnabled = false;
	bool mStreamlineInitialized = false;

	D3D12Resource mOutAlbedoSpecularTex; ///< Intermediate texture for Albedo + Specular input to DLSS.
	DescriptorHandle mUavHandle_AlbedoSpecular;
	DescriptorHandle mSrvHandle_AlbedoSpecular;

	D3D12Resource mOutAlbedoTex; ///< Intermediate texture for Albedo input to DLSS.
	DescriptorHandle mUavHandle_Albedo;
	DescriptorHandle mSrvHandle_Albedo;


	void InitializeDLSSRR();
	void UpdateCameraJitter();
	void EvaluateDLSSRR(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj, const DirectX::XMMATRIX& invView, const DirectX::XMMATRIX& invProj, const DirectX::XMFLOAT3& cameraPos, const DirectX::XMFLOAT3& cameraForward, float nearZ, float farZ, float fovY, float aspectRatio);
	void CleanupStreamline();

	void AllocateHandles();
	void InitializeDepthBuffer();
	void InitializeRayTracingPipelines();
	void BuildRayTracingAccelerationStructures();

	void ToneMap(D3D12_GPU_DESCRIPTOR_HANDLE srcSrvHandle, ID3D12Resource* srcResource);
public:
	void SetDLSSMode(sl::DLSSMode mode);
	sl::DLSSMode GetDLSSMode() const { return mDLSSMode; }

	// ===========================================
	// Tonemapping resources
	D3D12RootSignature mTonemapRootSignature;
	D3D12PipelineState mPipelineStateTonemap;
	D3D12Resource mTonemapOutputTexture;
	DescriptorHandle mUavHandle_TonemapOutput;
	//int mUavSlot_TonemapOutput = -1;
	void InitializeTonemapPipeline();
	void InitializeTonemapResources();

	HLSLCompiler mShaderCompiler; ///< HLSL shader compiler instance.

	bool mSceneLoaded = false; ///< Whether a scene is currently loaded.
	unsigned int mFrameCount = 0;

	// timing
	LARGE_INTEGER mPrevCounter{};
	double mSecondsPerCount = 0.0;


	void InitializeRootSignatures();
	// ImGui resources
	ImGuiContext* mImGuiContext = nullptr;
	DescriptorHeap mImGuiSrvHeap;
	HWND mHwnd = nullptr;

	D3D12CommandQueue mCommandQueue; ///< Command queue and fence synchronization (destroyed last).

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

	void UploadLightsToGPU();

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
	void UploadMeshes(const std::function<void()>& executeBatch, DirectX::ResourceUploadBatch& ddsBatch);

	/**
	* @brief Uploads GPU resources for a single mesh.
	* @param mesh Mesh to upload.
	* @param directory Directory of the model owning the mesh (for texture paths).
	* @param executeBatch Function to execute the command queue when needed.
	*/
	void UploadSingleMesh(const Mesh& mesh, const std::string& directory, const std::function<void()>& executeBatch, DirectX::ResourceUploadBatch& ddsBatch);

	/**
	 * @brief Creates material texture descriptors for a mesh.
	 * @param directory Directory of the model owning the mesh (for texture paths).
	 * @param mesh Mesh whose material to create.
	 * @param dst CPU descriptor handle where to write the material SRV descriptors.
	 * @param executeBatch Function to execute the command queue when needed.
	 */
	void CreateMaterial(const Mesh& mesh, const std::string& directory, MeshGpuData& gpuData, const std::function<void()>& executeBatch, DirectX::ResourceUploadBatch& ddsBatch);

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

	void SetAllResourcesNames();

	void RenderHybrid(const Camera& camera, size_t cbOffset);

public:
	/**
	 * @brief Creates device/swap chain and initializes resources.
	 * @param hwnd Window handle.
	 * @param width Client width.
	 * @param height Client height.
	 */
	void Initialize(HWND hwnd, UINT width, UINT height);

	/**
	 * @brief Destructor ensures proper cleanup of GPU resources.
	 */
	~Renderer();

	/**
	 * @brief Handles window resize by recreating swap chain buffers and depth buffer.
	 * @param width New client width.
	 * @param height New client height.
	 */
	void OnResize(UINT width, UINT height);

	/**
	 * @brief Records and submits commands for one frame and presents.
	 * @param viewProj View-projection matrix provided from external source (CameraManager).
	 * @param cameraPos Camera world position.
	 * @param cameraForward Camera forward direction.
	 */
	void Update(const Camera& camera);

	void CopyTonemapToSwapChain();

	/**
	 * @brief Loads a scene from an already-loaded Model (for async loading).
	 * @param model Unique pointer to a Model loaded on background thread.
	 * @return true if scene uploaded successfully, false otherwise.
	 */
	bool LoadSceneFromModel(std::unique_ptr<Model> model);

	/**
	 * @brief Loads multiple scenes from already-loaded Models.
	 * Handles memory limits by loading as many models as fit.
	 * @param models Vector of unique pointers to Models loaded on background thread.
	 * @return true if at least one scene uploaded successfully, false otherwise.
	 */
	bool LoadMultipleScenes(std::vector<std::unique_ptr<Model>> models);

	/**
	 * @brief Adds multiple extension scenes to the currently loaded scene.
	 * Does not unload existing models, only adds new ones.
	 * @param models Vector of unique pointers to Models loaded on background thread.
	 * @return true if at least one scene uploaded successfully, false otherwise.
	 */
	bool AddExtensionScenes(std::vector<std::unique_ptr<Model>> models);

	/**
	 * @brief Unloads the currently loaded scene and frees GPU resources.
	 */
	void UnloadScene();

	/**
	 * @brief Checks if a scene is currently loaded.
	 * @return true if scene is loaded, false otherwise.
	 */
	bool HasScene() const { return !mModels.empty(); }

	/**
	 * @brief Gets the number of loaded models.
	 * @return Number of models currently loaded.
	 */
	size_t GetLoadedModelsCount() const { return mModels.size(); }

	/**
	 * @brief Initializes ImGui for DirectX 12 rendering.
	 */
	void InitializeImGui(HWND hwnd);

	/**
	 * @brief Cleans up ImGui resources.
	 */
	void ShutdownImGui();

	/**
	 * @brief Begins a new ImGui frame.
	 */
	void BeginImGuiFrame();

	/**
	 * @brief Renders ImGui draw data.
	 */
	void RenderImGui();

	/**
	 * @brief Get the DXGI adapter used by the renderer.
	 * @return Pointer to IDXGIAdapter3, or nullptr if not available.
	 */
	IDXGIAdapter3* GetAdapter() const { return mAdapter.Get(); }


	void InitializeStreamline();


	void SetRenderMode(RenderMode mode) { mCurrentRenderMode = mode; }
	RenderMode GetRenderMode() const { return mCurrentRenderMode; }

	void SetShadowsEnabled(bool enabled) { mShadowsEnabled = enabled; }
	bool GetShadowsEnabled() const { return mShadowsEnabled; }
	void SetReflectionsEnabled(bool enabled) { mReflectionsEnabled = enabled; }
	bool GetReflectionsEnabled() const { return mReflectionsEnabled; }
	void SetMaxReflectionDepth(UINT depth) { mMaxReflectionDepth = std::clamp(depth, 1u, Config::cMaxReflectionDepth); }
	UINT GetMaxReflectionDepth() const { return mMaxReflectionDepth; }
	void SetMaxTransmissionDepth(UINT depth) { mMaxTransmissionDepth = std::clamp(depth, 0u, Config::cMaxTransmitionDepth); }
	UINT GetMaxTransmissionDepth() const { return mMaxTransmissionDepth; }


	void SetSunColor(float r, float g, float b);
	DirectX::XMFLOAT3 GetSunColor() const { return mSunColor; }

	void SetSunEnabled(bool enabled);
	bool GetSunEnabled() const { return mSunEnabled; }

	void SetSunDirection(float x, float y, float z);
	DirectX::XMFLOAT3 GetSunDirection() const { return mSunDirection; }

	void SetRISCandidates(UINT n) { mRISCandidates = std::clamp(n, 0u, Config::cMaxRISCandidates); }
	UINT GetRISCandidates() const { return mRISCandidates; }

	// Helper for UI slider range
	UINT GetNumPointLights() const { return mConstantBufferData.numPointLights; }

	void SetShadowRays(UINT n) { mPointShadowRays = std::clamp(n, 0u, Config::cMaxPointShadowRays); }
	UINT GetShadowRays() const { return mPointShadowRays; }
};

