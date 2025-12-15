#include "pch.h"
#include "D3D12/D3D12Debug.h"
#include "DataTypes.h"
#include "DXGI/DXGIDebug.h"
#include "DXGI/DXGIFactory.h"
#include "helpers.h"
#include "HLSL/HLSLCompiler.h"
#include "HLSL/HLSLShader.h"
#include "Input/InputManager.h"
#include "ResourceLoading/Model.h"
#include "ResourceLoading/TextureLoader.h"
#include "RenderAPI/Descriptors/ShaderVisibleDescriptorHeap.h"
#include "Renderer.h"
#include "paths.h"

// ImGui includes
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx12.h"

using std::wcout, std::endl, std::string, std::wstring, std::vector, std::unordered_map, std::function, std::future;


/**
* @brief Maps TextureType enum to descriptor slot index.
*/
static int TextureTypeToSlot(TextureType type)
{
	return static_cast<int>(type);
}

void Renderer::DispatchTextureDecoding()
{
	for (const auto& modelPtr : mModels)
	{
		for (const Mesh& mesh : modelPtr->mMeshes)
		{
			for (const Texture& cpuTex : mesh.mTextures)
			{
				string fullPath = modelPtr->mDirectory + "\\" + cpuTex.mPath;
				auto it = mTextureCache.find(fullPath);
				if (it == mTextureCache.end())
				{
					auto fut = std::async(std::launch::async, ImageDecoder::DecodeImageRGBA8_ThreadSafe, wstring(fullPath.begin(), fullPath.end()));
					mTextureCache[fullPath].decodeFuture = std::move(fut);
				}
			}
		}
	}
}

void Renderer::CreateMaterial(const Mesh& mesh, const string& directory, MeshGpuData& gpuData, const function<void()>& executeBatch)
{
	unordered_map<int, string> textureMap;
	for (const Texture& cpuTex : mesh.mTextures)
		textureMap[TextureTypeToSlot(cpuTex.mType)] = cpuTex.mPath;

	gpuData.materialTable = mSrvHeap.Allocate(Config::cNumberOfTextureSlots);


	auto processSlot = [&](int slotIndex, const GPUTexture& fallback)
		{
			D3D12_CPU_DESCRIPTOR_HANDLE dst = gpuData.materialTable.cpuHandle;
			dst.ptr += SIZE_T(slotIndex) * mDevice.Get()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

			auto it = textureMap.find(slotIndex);
			if (it != textureMap.end())
			{
				string fullPath = directory + "\\" + it->second;
				GPUTextureLoadState& loadState = mTextureCache[fullPath];
				if (loadState.decodeFuture.valid())
					loadState.decodedImage = loadState.decodeFuture.get();

				if (!loadState.gpuTexture.resource.Get())
				{
					loadState.gpuTexture = mTextureLoader.CreateTextureFromDecodedImage(loadState.decodedImage, executeBatch);
					loadState.decodedImage = {}; // free CPU-side decoded image data
				}
				GPUTexture& gpuTex = loadState.gpuTexture;
				CreateTextureView(gpuTex.resource.Get(), gpuTex.format, dst, gpuTex.mipLevels);
			}
			else
			{
				CreateTextureView(fallback.resource.Get(), fallback.format, dst, fallback.mipLevels);
			}
		};

	processSlot(0, mDefaultTextures.white);  // Albedo
	processSlot(1, mDefaultTextures.white);  // Metallic
	processSlot(2, mDefaultTextures.white);  // Roughness
	processSlot(3, mDefaultTextures.normal); // Normal
	processSlot(4, mDefaultTextures.white);  // Emissive	
}

void Renderer::UploadSingleMesh(const Mesh& mesh, const string& directory, const function<void()>& executeBatch)
{
	const size_t vbSize = mesh.mVertices.size() * sizeof(::Vertex);
	const size_t ibSize = mesh.mIndices.size() * sizeof(unsigned int);
	const UINT vbSizeUINT = static_cast<UINT>(vbSize);
	const UINT ibSizeUINT = static_cast<UINT>(ibSize);
	const size_t needed = vbSize + ibSize;

	if (!mUploadHeap.CanAllocate(needed))
		executeBatch();

	MeshGpuData gpu{};
	gpu.materialData = mesh.mMaterialData;
	gpu.vb.Initialize(mDevice.Get(), vbSizeUINT, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
	gpu.ib.Initialize(mDevice.Get(), ibSizeUINT, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);

	auto vbAlloc = mUploadHeap.Allocate(vbSize);
	auto ibAlloc = mUploadHeap.Allocate(ibSize);

	if (!vbAlloc.cpuPtr || !ibAlloc.cpuPtr)
		throw std::runtime_error("Upload heap out of space for mesh buffers batch.");

	memcpy(vbAlloc.cpuPtr, mesh.mVertices.data(), vbSize);
	memcpy(ibAlloc.cpuPtr, mesh.mIndices.data(), ibSize);

	mCommandList.Get()->CopyBufferRegion(gpu.vb.Get(), 0, mUploadHeap.GetResource(), vbAlloc.offset, vbSize);
	mCommandList.Get()->CopyBufferRegion(gpu.ib.Get(), 0, mUploadHeap.GetResource(), ibAlloc.offset, ibSize);

	D3D12_RESOURCE_BARRIER barriers[2] = {};
	barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[0].Transition.pResource = gpu.vb.Get();
	barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;

	barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[1].Transition.pResource = gpu.ib.Get();
	barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;

	mCommandList.Get()->ResourceBarrier(_countof(barriers), barriers);

	gpu.vbv.BufferLocation = gpu.vb.Get()->GetGPUVirtualAddress();
	gpu.vbv.SizeInBytes = vbSizeUINT;
	gpu.vbv.StrideInBytes = sizeof(::Vertex);
	gpu.ibv.BufferLocation = gpu.ib.Get()->GetGPUVirtualAddress();
	gpu.ibv.SizeInBytes = ibSizeUINT;
	gpu.ibv.Format = DXGI_FORMAT_R32_UINT;

	CreateMaterial(mesh, directory, gpu, executeBatch);

	gpu.center = mesh.mCenter;
	switch (mesh.mRenderLayer)
	{
	case RenderLayer::Opaque:
		if (mesh.mDoubleSided)
			mOpaqueDoubleSidedMeshes.push_back(std::move(gpu));
		else
			mOpaqueSingleSidedMeshes.push_back(std::move(gpu));
		break;
	case RenderLayer::Masked:
		if (mesh.mDoubleSided)
			mMaskedDoubleSidedMeshes.push_back(std::move(gpu));
		else
			mMaskedSingleMeshes.push_back(std::move(gpu));
		break;
	case RenderLayer::Blend:
		mTransparentMeshes.push_back(std::move(gpu));
		break;
	default:
		break;
	}
}

void Renderer::UploadMeshes(const function<void()>& executeBatch)
{
	for (const auto& modelPtr : mModels)
	{
		for (const Mesh& mesh : modelPtr->mMeshes)
			UploadSingleMesh(mesh, modelPtr->mDirectory, executeBatch);
	}
}

void Renderer::BuildMeshGpuData()
{
    mUploadHeap.Reset();
    mCommandList.ResetCommandList(0); // Use allocator 0 for one-time upload, not swap chain index

	auto executeBatch = [this]()
		{
			mCommandList.Get()->Close();
			ID3D12CommandList* lists[] = { mCommandList.Get() };
			mCommandQueue.ExecuteCommandLists(1, lists);
			mCommandQueue.Flush();

        mUploadHeap.Reset();
        mCommandList.ResetCommandList(0); // Always use allocator 0 for uploads
        mTextureLoader.Reset();
    };

	DispatchTextureDecoding();
	UploadMeshes(executeBatch);

	executeBatch();
	mCommandList.Get()->Close();
}

void Renderer::CollectStaticLights()
{
	mStaticLights.clear();
	for (const auto& modelPtr : mModels)
	{
		if (!modelPtr) continue;
		for (const auto& l : modelPtr->mLights)
		{
			mStaticLights.push_back(l);
			if (mStaticLights.size() >= cMaxLights) break;
		}
		if (mStaticLights.size() >= cMaxLights) break;
	}

	// Copy static lights once into CPU-side constant buffer data so Update doesn't have to re-create them.
	int staticCount = static_cast<int>(std::min<size_t>(mStaticLights.size(), cMaxLights));
	for (int i = 0; i < staticCount; ++i)
	{
		mConstantBufferData.lights[i] = mStaticLights[i];
	}
	// Set numLights to static count for now; Update will adjust (append camera light) each frame if needed.

	if (mStaticLights.size() < cMaxLights)
	{
		LightData sunLight{};
		//sunLight.dirType = DirectX::XMFLOAT4(0.5f, -1.0f, 0.5f, 1.0f); // directional light
		sunLight.dirType = DirectX::XMFLOAT4(0.2f, -1.0f, 0.2f, 1.0f); // directional flag
		sunLight.diffuseColor = DirectX::XMFLOAT4(1.0f, 1.0f, 0.9f, 1.0f);
		sunLight.specularColor = DirectX::XMFLOAT4(1.0f, 1.0f, 0.9f, 1.0f);
		mStaticLights.push_back(sunLight);
		mConstantBufferData.lights[staticCount] = sunLight;
		staticCount++;
	}

	mConstantBufferData.numLights = staticCount;

	{
		void* pData;
		D3D12_RANGE readRange = { 0, 0 }; // We do not intend to read from this resource on the CPU.

		HRESULT hr = mGlobalLightBuffer.Get()->Map(0, &readRange, &pData);
		ASSERT_HR(hr, "Failed to map constant buffer for light update.");

		memcpy(pData, mStaticLights.data(), mStaticLights.size() * sizeof(LightData));
		mGlobalLightBuffer.Get()->Unmap(0, nullptr);
	}
}

void Renderer::CreateTextureView(ID3D12Resource* resource, DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE handle, UINT mipLevels)
{
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = mipLevels;
	mDevice.Get()->CreateShaderResourceView(resource, &srvDesc, handle);
}


void Renderer::Initialize(HWND hwnd, UINT width, UINT height)
{
#ifdef _DEBUG
	D3D12Debug::GetInstance().Enable();
	DXGIDebug::GetInstance().Enable();
#endif

	DXGIFactory factory;
	DXGIAdapter adapter = factory.GetAdapter();

	DXGI_ADAPTER_DESC desc;
	HRESULT hr = adapter->GetDesc(&desc);
	CHECK_HR(hr, "Failed to get adapter description.");

	wcout << "Selected device: " << desc.Description << endl;

    // Store adapter for VRAM queries
    mAdapter = adapter;

    mHwnd = hwnd;
    mDevice.Initialize(adapter.Get());
    mCommandQueue.Initialize(mDevice.Get());
    mCommandList.Initialize(mDevice.Get());
    mSwapChain.Initialize(factory.Get(), hwnd, mCommandQueue.Get(), mDevice.Get(), width, height);

	mDepthBuffer.Initialize(mDevice.Get(), width, height);
	//D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	//dsvDesc.Format = Config::cDepthBufferFormat;
	//dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	//mDevice.Get()->CreateDepthStencilView(
	//    mDepthBuffer.GetResource(),
	//    &dsvDesc,
	//    mDepthBuffer.GetDSVHandle()
	//);

	mWidth = width;
	mHeight = height;

	// setup timer for delta time
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	mSecondsPerCount = 1.0 / static_cast<double>(freq.QuadPart);
	QueryPerformanceCounter(&mPrevCounter);

	// Shader-visible SRV heap for textures (increase capacity for many material descriptors)
	mSrvHeap.Initialize(mDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, Config::cNumberOfSrvDescriptors, true);

	// Shared upload heap (64 MB)
	mUploadHeap.Initialize(mDevice.Get(), 512ull * 1024ull * 1024ull);

	mShaderCompiler.Initialize();

	InitializeTextureLoader();

	InitializeRootSignatures();

	InitializePipelineState();

	InitializeGBufferResources();
	InitializeComputePipeline();
	CreateLightBuffer();

	InitializeReflectionResources();

	InitializeDummyTextures();

	mRayTracingBuilder.Initialize(mDevice.Get(), mCommandList.Get(), &mCommandQueue);

	// set viewport and scissor rect
	mViewport.TopLeftX = 0.0f;
	mViewport.TopLeftY = 0.0f;
	mViewport.Width = static_cast<FLOAT>(mWidth);
	mViewport.Height = static_cast<FLOAT>(mHeight);
	mViewport.MinDepth = 0.0f;
	mViewport.MaxDepth = 1.0f;

	mScissorRect.left = 0;
	mScissorRect.top = 0;
	mScissorRect.right = static_cast<LONG>(mWidth);
	mScissorRect.bottom = static_cast<LONG>(mHeight);

	// view-projection matrix (will be updated each frame)
	mConstantBufferData.vpMatrix = DirectX::XMMatrixIdentity();

	UINT alignedSize = (sizeof(ConstantBufferData) + 255) & ~255; // Align to 256 bytes

	mConstantBuffer.Initialize(
		mDevice.Get(),
		alignedSize * 3,
		D3D12_HEAP_TYPE_UPLOAD,
		D3D12_RESOURCE_STATE_GENERIC_READ);


    // Initialize ImGui at the end of initialization
    InitializeImGui(hwnd);

	// For debugging: recompile shaders on 'G' key press
	InputManager::Instance.RegisterKeyPressedCallback('G', std::bind(&Renderer::InitializePipelineState, this));
}

Renderer::~Renderer()
{
	wcout << L"Renderer destructor: cleaning up GPU resources..." << endl;
	
	// Wait for all GPU operations to complete before destroying resources
	mCommandQueue.Flush();
	
	// Shutdown ImGui first (it uses our descriptor heaps)
	ShutdownImGui();
	
	// Unload scene resources (meshes, textures, models)
	UnloadScene();
	
	// Clear all remaining GPU resources
	// Pipeline states, command lists, etc. will be released by their destructors
	// but we want to ensure everything is done in the right order
	
	wcout << L"Renderer cleanup complete" << endl;
}

void Renderer::OnResize(UINT width, UINT height)
{
	if (width == 0 || height == 0)
		return; // Ignore invalid sizes (minimized window)

	if (width == mWidth && height == mHeight)
		return; // No actual resize

	wcout << "Resizing renderer to " << width << "x" << height << endl;

	// 1. Wait for GPU to complete all work
	mCommandQueue.Flush();

	// 2. Update dimensions
	mWidth = width;
	mHeight = height;

	// 2. Resize SwapChain
	mSwapChain.Resize(width, height);

	// =========================================================================
	// 4. Resize Depth Buffer & Update Descriptors
	// =========================================================================
	{
		mDepthBuffer.Initialize(mDevice.Get(), width, height);
		D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
		dsvDesc.Format = Config::cDepthBufferFormat;
		dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

		mDevice.Get()->CreateDepthStencilView(
			mDepthBuffer.GetResource(),
			&dsvDesc,
			mDepthBuffer.GetDSVHandle()
		);

		D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv = {};
		depthSrv.Format = DXGI_FORMAT_R32_FLOAT; // Must use R32_FLOAT to read R32_TYPELESS
		depthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		depthSrv.Texture2D.MipLevels = 1;
		depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

		// This writes the NEW resource pointer into the OLD slot index
		mDevice.Get()->CreateShaderResourceView(
			mDepthBuffer.GetResource(),
			&depthSrv,
			mSrvHeap.GetCpuHandle(mSrvSlot_Depth)
		);
	}

	// =========================================================================
	// 5. Resize G-Buffers
	// =========================================================================
	{
		// Albedo
		auto albedoDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R8G8B8A8_UNORM, mWidth, mHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
		D3D12_CLEAR_VALUE clearBlack = { DXGI_FORMAT_R8G8B8A8_UNORM, { 0.0f, 0.0f, 0.0f, 1.0f } };
		mGBufferAlbedo.Initialize(mDevice.Get(), albedoDesc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON, &clearBlack);

		// Normal
		auto normalDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, mWidth, mHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
		D3D12_CLEAR_VALUE clearNormal = { DXGI_FORMAT_R16G16B16A16_FLOAT, { 0.5f, 0.5f, 1.0f, 1.0f } };
		mGBufferNormal.Initialize(mDevice.Get(), normalDesc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON, &clearNormal);

		// Material
		auto materialDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R32G32_FLOAT, mWidth, mHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
		D3D12_CLEAR_VALUE clearMaterial = { DXGI_FORMAT_R32G32_FLOAT, { 0.0f, 0.0f, 0.0f, 1.0f } };
		mGBufferMaterial.Initialize(mDevice.Get(), materialDesc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON, &clearMaterial);

		// Emissive
		auto emissiveDesc = CD3DX12_RESOURCE_DESC::Tex2D(DXGI_FORMAT_R16G16B16A16_FLOAT, mWidth, mHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET);
		D3D12_CLEAR_VALUE clearEmissive = { DXGI_FORMAT_R16G16B16A16_FLOAT, { 0.0f, 0.0f, 0.0f, 1.0f } };
		mGBufferEmission.Initialize(mDevice.Get(), emissiveDesc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON, &clearEmissive);

		// Update SRVs (Use existing slots)
		CreateTextureView(mGBufferAlbedo.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, mSrvHeap.GetCpuHandle(mSrvSlot_GBufferAlbedo), 1);
		CreateTextureView(mGBufferNormal.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, mSrvHeap.GetCpuHandle(mSrvSlot_GBufferNormal), 1);
		CreateTextureView(mGBufferMaterial.Get(), DXGI_FORMAT_R32G32_FLOAT, mSrvHeap.GetCpuHandle(mSrvSlot_GBufferMaterial), 1);
		CreateTextureView(mGBufferEmission.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, mSrvHeap.GetCpuHandle(mSrvSlot_GBufferEmissive), 1);

		// Update RTVs (Render Target Views)
		// Assumption: RTV Heap was allocated linearly: 0=Albedo, 1=Normal, 2=Material, 3=Emissive
		mDevice.Get()->CreateRenderTargetView(mGBufferAlbedo.Get(), nullptr, mGBufferRtvHeap.GetCpuHandle(0));
		mDevice.Get()->CreateRenderTargetView(mGBufferNormal.Get(), nullptr, mGBufferRtvHeap.GetCpuHandle(1));
		mDevice.Get()->CreateRenderTargetView(mGBufferMaterial.Get(), nullptr, mGBufferRtvHeap.GetCpuHandle(2));
		mDevice.Get()->CreateRenderTargetView(mGBufferEmission.Get(), nullptr, mGBufferRtvHeap.GetCpuHandle(3));
	}

	// =========================================================================
	// 6. Resize Compute Output (Fixes CopyResource Error)
	// =========================================================================
	{
		auto uavDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R8G8B8A8_UNORM, mWidth, mHeight, 1, 1, 1, 0,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
		);
		mComputeOutputTexture.Initialize(mDevice.Get(), uavDesc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);

		// Update UAV
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavViewDesc = {};
		uavViewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		uavViewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		mDevice.Get()->CreateUnorderedAccessView(mComputeOutputTexture.Get(), nullptr, &uavViewDesc, mSrvHeap.GetCpuHandle(mUavSlot_Output));

		// Update RTV
		mDevice.Get()->CreateRenderTargetView(mComputeOutputTexture.Get(), nullptr, mGBufferRtvHeap.GetCpuHandle(mRtvIndex_ComputeOutput));
	}

	// =========================================================================
	// 7. Resize Reflection Targets
	// =========================================================================
	{
		auto desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R16G16B16A16_FLOAT, mWidth, mHeight, 1, 1, 1, 0, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);
		mOutDiffuseTex.Initialize(mDevice.Get(), desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
		mOutSpecularTex.Initialize(mDevice.Get(), desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);

		// Update UAVs
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		mDevice.Get()->CreateUnorderedAccessView(mOutDiffuseTex.Get(), nullptr, &uavDesc, mSrvHeap.GetCpuHandle(mUavSlot_Diffuse));
		mDevice.Get()->CreateUnorderedAccessView(mOutSpecularTex.Get(), nullptr, &uavDesc, mSrvHeap.GetCpuHandle(mUavSlot_Specular));

		// Update SRVs
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		mDevice.Get()->CreateShaderResourceView(mOutDiffuseTex.Get(), &srvDesc, mSrvHeap.GetCpuHandle(mSrvSlot_Diffuse));
		mDevice.Get()->CreateShaderResourceView(mOutSpecularTex.Get(), &srvDesc, mSrvHeap.GetCpuHandle(mSrvSlot_Specular));
	}

	// 8. Update Viewport and Scissor Rect
	//mViewport.TopLeftX = 0.0f;
	//mViewport.TopLeftY = 0.0f;
	mViewport.Width = static_cast<FLOAT>(width);
	mViewport.Height = static_cast<FLOAT>(height);
	//mViewport.MinDepth = 0.0f;
	//mViewport.MaxDepth = 1.0f;

	// Update scissor rect
	//mScissorRect.left = 0;
	//mScissorRect.top = 0;
	mScissorRect.right = static_cast<LONG>(width);
	mScissorRect.bottom = static_cast<LONG>(height);

	wcout << "Resize complete!" << endl;
}

bool Renderer::LoadScene(const std::string& path)
{
    wcout << L"Loading scene: " << wstring(path.begin(), path.end()) << endl;

    auto model = std::make_unique<Model>();

    std::chrono::steady_clock::time_point loadStartTime = std::chrono::steady_clock::now();
    model->loadModel(path);
    std::chrono::steady_clock::time_point loadEndTime = std::chrono::steady_clock::now();
    std::chrono::duration<double> loadElapsedSeconds = loadEndTime - loadStartTime;
    
    if (model->mMeshes.empty())
    {
        wcout << L"Warning: Scene loaded but contains no meshes." << endl;

        // Create fallback quad for testing
        vector<::Vertex> cpuVerts = {
            { { -1, -1, 0 }, {0,0,1}, {0,1} },
            { { -1,  1, 0 }, {0,0,1}, {0,0} },
            { {  1,  1, 0 }, {0,0,1}, {1,0} },
            { {  1, -1, 0 }, {0,0,1}, {1,1} },
        };
        vector<unsigned int> cpuIdx = { 0,1,2, 0,2,3 };
        model->mMeshes.push_back(Mesh(cpuVerts, cpuIdx, {}));
        return false;
    }

    wcout << L"Model loaded in " << loadElapsedSeconds.count() << L" seconds." << endl;

    std::vector<std::unique_ptr<Model>> models;
    models.push_back(std::move(model));
    return LoadMultipleScenes(std::move(models));
}

bool Renderer::LoadSceneFromModel(std::unique_ptr<Model> model)
{
	if (!model)
		return false;

	std::vector<std::unique_ptr<Model>> models;
	models.push_back(std::move(model));
	return LoadMultipleScenes(std::move(models));
}

bool Renderer::LoadMultipleScenes(std::vector<std::unique_ptr<Model>> models)
{
	mSceneLoaded = false;
	if (models.empty())
		return false;

	wcout << L"Loading " << models.size() << L" scene(s)..." << endl;

	// Wait for GPU to finish all work before loading new scenes
	mCommandQueue.Flush();

	// Try to load models one by one, catching any memory allocation failures
	size_t successfullyLoaded = 0;
	size_t totalAttempted = models.size();
	
	for (size_t i = 0; i < models.size(); ++i)
	{
		auto& model = models[i];
		if (!model)
			continue;

		try
		{
			// Check if we have meshes before trying to add
			if (model->mMeshes.empty())
			{
				wcout << L"Warning: Model " << (i + 1) << L" has no meshes, skipping." << endl;
				continue;
			}
			
			// Try to add the model
			mModels.push_back(std::move(model));
			successfullyLoaded++;
			
			wcout << L"Successfully added model " << successfullyLoaded << L" / " << totalAttempted << endl;
		}
		catch (const std::bad_alloc& e)
		{
			wcout << L"Memory allocation failed at model " << (i + 1) << L": " << e.what() << endl;
			wcout << L"Stopping further model loading due to memory constraints." << endl;
			break;
		}
		catch (const std::exception& e)
		{
			wcout << L"Error adding model " << (i + 1) << L": " << e.what() << endl;
			// Continue trying with next model
			continue;
		}
	}

	if (successfullyLoaded == 0)
	{
		wcout << L"Error: No models could be loaded." << endl;
		return false;
	}

	wcout << L"Added " << successfullyLoaded << L" / " << totalAttempted << L" models to scene." << endl;

	// Try to build GPU data for all loaded models
    try
    {
        std::chrono::steady_clock::time_point meshBuildStartTime = std::chrono::steady_clock::now();
        BuildMeshGpuData();
        std::chrono::steady_clock::time_point meshBuildEndTime = std::chrono::steady_clock::now();
        std::chrono::duration<double> elapsedSeconds = meshBuildEndTime - meshBuildStartTime;
        wcout << "Mesh GPU data built in " << elapsedSeconds.count() << " seconds." << endl;
    }
    catch (const std::runtime_error& e)
    {
        wcout << L"GPU memory exhausted during mesh data upload: " << e.what() << endl;
        wcout << L"Some models may not be fully loaded. Try loading fewer or smaller models." << endl;
        
        // If BuildMeshGpuData fails due to GPU memory, we still have valid models in CPU memory
        // but their GPU data may be incomplete. Better to unload them completely.
        if (mOpaqueSingleSidedMeshes.empty() && mOpaqueDoubleSidedMeshes.empty() && 
		    mMaskedSingleMeshes.empty() && mMaskedDoubleSidedMeshes.empty() && 
		    mTransparentMeshes.empty())
		{
			// No meshes were uploaded at all - complete failure
			wcout << L"No meshes could be uploaded to GPU. Unloading all models." << endl;
			mModels.clear();
			mTextureCache.clear();
			return false;
		}
		// else: Some meshes were uploaded, continue with what we have
    }
    catch (const std::exception& e)
    {
        wcout << L"Error building mesh GPU data: " << e.what() << endl;
        
        // Check if we have at least some meshes uploaded
        if (mOpaqueSingleSidedMeshes.empty() && mOpaqueDoubleSidedMeshes.empty() && 
		    mMaskedSingleMeshes.empty() && mMaskedDoubleSidedMeshes.empty() && 
		    mTransparentMeshes.empty())
		{
			wcout << L"No meshes could be uploaded to GPU. Unloading all models." << endl;
			mModels.clear();
			mTextureCache.clear();
			return false;
		}
    }
	
	// Try to build ray tracing structures
	try
	{
		std::chrono::steady_clock::time_point rtBuildStartTime = std::chrono::steady_clock::now();
		InitializeRayTracing();
		std::chrono::steady_clock::time_point rtBuildEndTime = std::chrono::steady_clock::now();
		std::chrono::duration<double> rtElapsedSeconds = rtBuildEndTime - rtBuildStartTime;
		wcout << "Ray tracing structures built in " << rtElapsedSeconds.count() << " seconds." << endl;
	}
	catch (const std::exception& e)
	{
		wcout << L"Warning: Failed to build ray tracing structures: " << e.what() << endl;
		// Non-critical, continue without RT acceleration
	}

	// Collect lights (this should be safe)
	try
	{
		CollectStaticLights();
	}
	catch (const std::exception& e)
	{
		wcout << L"Warning: Failed to collect lights: " << e.what() << endl;
	}

	size_t loadedMeshCount = mOpaqueSingleSidedMeshes.size() + mOpaqueDoubleSidedMeshes.size() + 
	                         mMaskedSingleMeshes.size() + mMaskedDoubleSidedMeshes.size() + 
	                         mTransparentMeshes.size();
	
	if (loadedMeshCount > 0)
	{
		wcout << L"Scene(s) loaded successfully! (" << successfullyLoaded << L" model(s), " 
		      << loadedMeshCount << L" meshes)" << endl;
		mSceneLoaded = true;
		return true;
	}
	else
	{
		wcout << L"No meshes could be loaded to GPU." << endl;
		return false;
	}
}

void Renderer::UnloadScene()
{
    wcout << L"Unloading scene..." << endl;

    // Wait for GPU to finish all work
    mCommandQueue.Flush();

    // Clear all GPU resources - now we have separate mesh lists
    mOpaqueSingleSidedMeshes.clear();
    mOpaqueDoubleSidedMeshes.clear();
    mMaskedSingleMeshes.clear();
    mMaskedDoubleSidedMeshes.clear();
    mTransparentMeshes.clear();
    
    mModels.clear();
    mTextureCache.clear();
    mStaticLights.clear();

    // Reset heaps
    mUploadHeap.Reset();
    mTextureLoader.Reset();

    // Reset constant buffer data
    mConstantBufferData.numLights = 0;
    for (int i = 0; i < cMaxLights; ++i)
    {
        mConstantBufferData.lights[i] = LightData{};
    }

    wcout << L"Scene unloaded." << endl;
}

void Renderer::InitializeDummyTextures()
{
    mUploadHeap.Reset();
    mCommandList.ResetCommandList(mSwapChain.GetCurrentBackBufferIndex());
    mDefaultTextures =
    {
        .white = mTextureLoader.CreateSolidDummyTexture(0xFFFFFFFF),    // (255,255,255) in BGRA
        //.black = mTextureLoader.CreateSolidDummyTexture(0xFF000000),    // (0,0,0) in BGRA
        .normal = mTextureLoader.CreateSolidDummyTexture(0xFFFF8080),   // (255,128,128) in BGRA
    };
    mCommandList.Get()->Close();
    ID3D12CommandList* lists[] = { mCommandList.Get() };
    mCommandQueue.ExecuteCommandLists(1, lists);
    mCommandQueue.Flush();
}



void Renderer::InitializeRayTracing()
{
	mCommandList.ResetCommandList(mSwapChain.GetCurrentBackBufferIndex());

	// 1. Build BLAS for all mesh lists
	mRayTracingBuilder.BuildAllBLAS(
		mOpaqueSingleSidedMeshes,
		mOpaqueDoubleSidedMeshes,
		mMaskedSingleMeshes,
		mMaskedDoubleSidedMeshes,
		mTransparentMeshes);

	// 2. Allocate TLAS instance desc buffer
	UINT totalMeshes = static_cast<UINT>(
		mOpaqueSingleSidedMeshes.size() +
		mOpaqueDoubleSidedMeshes.size() +
		mMaskedSingleMeshes.size() +
		mMaskedDoubleSidedMeshes.size() +
		mTransparentMeshes.size());

	UINT64 instanceDescSize = sizeof(D3D12_RAYTRACING_INSTANCE_DESC) * totalMeshes;
	mInstanceDescBuffer.Initialize(
		mDevice.Get(),
		instanceDescSize,
		D3D12_HEAP_TYPE_UPLOAD,
		D3D12_RESOURCE_STATE_GENERIC_READ);

	// 3. Build TLAS
	mRayTracingBuilder.BuildTLAS(
		mOpaqueSingleSidedMeshes,
		mOpaqueDoubleSidedMeshes,
		mMaskedSingleMeshes,
		mMaskedDoubleSidedMeshes,
		mTransparentMeshes,
		mTLAS,
		mTLAS_Scratch,
		mInstanceDescBuffer);

	// 4. Execute command list
	mCommandList.Get()->Close();
	ID3D12CommandList* lists[] = { mCommandList.Get() };
	mCommandQueue.ExecuteCommandLists(1, lists);
	mCommandQueue.Flush();

	// 5. Clear temporary BLAS resources
	mRayTracingBuilder.ClearScratchResources();


	// Reflectios pipeline
	// 1. mComputeRootSignature can be reused as global root signature

	// 2. Local root signature
	// 2. Build Local Root Signature 
	CD3DX12_ROOT_PARAMETER1 localParams[3]{};

	// Param 0: Index buffer (t0)
	localParams[0].InitAsShaderResourceView(0, 1);

	// Param 1: Vertex buffer (t1)
	localParams[1].InitAsShaderResourceView(1, 1);

	// Param 2: Texture table (t2)
	CD3DX12_DESCRIPTOR_RANGE1 texRange{};
	texRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 2, 1); // t0-t4 space1
	localParams[2].InitAsDescriptorTable(1, &texRange);

	CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_ANISOTROPIC); // s0

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC localDesc{};
	localDesc.Init_1_1(3, localParams, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

	D3D12RootSignature localRootSig;
	localRootSig.Initialize(mDevice.Get(), localDesc);

	HLSLShader libraryShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/Reflections.hlsl", L"lib_6_3", {}, L"");

	// 3. Initialize Pipeline
	mReflectionsPipeline.Initialize(mDevice.Get(), &mComputeRootSignature, &localRootSig, libraryShader.GetShaderBlob());

	// 4. Build Shader Binding Table (SBT)
	std::vector<MeshGpuData> allMeshes;
	allMeshes.insert(allMeshes.end(), mOpaqueSingleSidedMeshes.begin(), mOpaqueSingleSidedMeshes.end());
	allMeshes.insert(allMeshes.end(), mOpaqueDoubleSidedMeshes.begin(), mOpaqueDoubleSidedMeshes.end());
	allMeshes.insert(allMeshes.end(), mMaskedSingleMeshes.begin(), mMaskedSingleMeshes.end());
	allMeshes.insert(allMeshes.end(), mMaskedDoubleSidedMeshes.begin(), mMaskedDoubleSidedMeshes.end());
	allMeshes.insert(allMeshes.end(), mTransparentMeshes.begin(), mTransparentMeshes.end());

	mReflectionsPipeline.BuildSBT(mDevice.Get(), allMeshes);
}


void Renderer::InitializeGBufferResources()
{
	// 1. Initialize Heaps
	// Create RTV Heap (Capacity 5, Not Visible)
	mGBufferRtvHeap.Initialize(mDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 5, false);

	// 2. Define Resource Descriptors (Standard D3DX12 code...)
	auto albedoDescc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R8G8B8A8_UNORM,
		mWidth,
		mHeight,
		1, // array size
		1, // mip levels
		1, // sample count
		0, // sample quality
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
	);
	auto normalDescc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		mWidth,
		mHeight,
		1, // array size
		1, // mip levels
		1, // sample count
		0, // sample quality
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
	);
	auto materialDescc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R32G32_FLOAT,
		mWidth,
		mHeight,
		1, // array size
		1, // mip levels
		1, // sample count
		0, // sample quality
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
	);
	auto emissiveDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		mWidth,
		mHeight,
		1, // array size
		1, // mip levels
		1, // sample count
		0, // sample quality
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
	);

	// Clear Values...
	D3D12_CLEAR_VALUE clearBlack = { DXGI_FORMAT_R8G8B8A8_UNORM, { 0.0f, 0.0f, 0.0f, 1.0f } };
	D3D12_CLEAR_VALUE clearNormal = { DXGI_FORMAT_R16G16B16A16_FLOAT, { 0.5f, 0.5f, 1.0f, 1.0f } };
	D3D12_CLEAR_VALUE clearMaterial = { DXGI_FORMAT_R32G32_FLOAT, { 0.0f, 0.0f, 0.0f, 1.0f } };
	D3D12_CLEAR_VALUE clearEmissive = { DXGI_FORMAT_R16G16B16A16_FLOAT, { 0.0f, 0.0f, 0.0f, 1.0f } };

	// Create Resources...
	mGBufferAlbedo.Initialize(
		mDevice.Get(),
		albedoDescc,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_STATE_COMMON,
		&clearBlack);
	mGBufferNormal.Initialize(
		mDevice.Get(),
		normalDescc,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_STATE_COMMON,
		&clearNormal);
	mGBufferMaterial.Initialize(
		mDevice.Get(),
		materialDescc,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_STATE_COMMON,
		&clearMaterial);
	mGBufferEmission.Initialize(
		mDevice.Get(),
		emissiveDesc,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_STATE_COMMON,
		&clearEmissive);

	// 3. Create RTVs using the Wrapper
	// Allocate 3 slots
	auto rtvAlbedoHandle = mGBufferRtvHeap.Allocate();
	auto rtvNormalHandle = mGBufferRtvHeap.Allocate();
	auto rtvMaterialHandle = mGBufferRtvHeap.Allocate();
	auto rtvEmissiveHandle = mGBufferRtvHeap.Allocate();

	// Create Views into the allocated handles
	mDevice.Get()->CreateRenderTargetView(
		mGBufferAlbedo.Get(),
		nullptr,
		rtvAlbedoHandle.cpuHandle);
	mDevice.Get()->CreateRenderTargetView(
		mGBufferNormal.Get(),
		nullptr,
		rtvNormalHandle.cpuHandle);
	mDevice.Get()->CreateRenderTargetView(
		mGBufferMaterial.Get(),
		nullptr,
		rtvMaterialHandle.cpuHandle);
	mDevice.Get()->CreateRenderTargetView(
		mGBufferEmission.Get(),
		nullptr,
		rtvEmissiveHandle.cpuHandle);

	// 4. Create SRVs (Inputs for Compute)
	auto srvAlbedo = mSrvHeap.Allocate();
	auto srvNormal = mSrvHeap.Allocate();
	auto srvMaterial = mSrvHeap.Allocate();
	auto srvDepth = mSrvHeap.Allocate();
	auto srvEmissive = mSrvHeap.Allocate();

	mSrvSlot_GBufferAlbedo = srvAlbedo.index;
	mSrvSlot_GBufferNormal = srvNormal.index;
	mSrvSlot_GBufferMaterial = srvMaterial.index;
	mSrvSlot_GBufferEmissive = srvEmissive.index;
	mSrvSlot_Depth = srvDepth.index;


	CreateTextureView(mGBufferAlbedo.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, srvAlbedo.cpuHandle, 1);
	CreateTextureView(mGBufferNormal.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, srvNormal.cpuHandle, 1);
	CreateTextureView(mGBufferMaterial.Get(), DXGI_FORMAT_R32G32_FLOAT, srvMaterial.cpuHandle, 1);
	CreateTextureView(mGBufferEmission.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, srvEmissive.cpuHandle, 1);

	D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv = {};
	depthSrv.Format = DXGI_FORMAT_R32_FLOAT;
	depthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	depthSrv.Texture2D.MipLevels = 1;
	depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	mDevice.Get()->CreateShaderResourceView(mDepthBuffer.GetResource(), &depthSrv, srvDepth.cpuHandle);
}

void Renderer::InitializeComputePipeline()
{
	// ====================================================================================
	// 1. Create Final Output Texture (UAV)
	// This is where the Composite Shader writes the merged result.
	// ====================================================================================
	auto uavDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R8G8B8A8_UNORM,
		mWidth,
		mHeight,
		1, // array size
		1, // mip levels
		1, // sample count
		0, // sample quality
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
	);

	mComputeOutputTexture.Initialize(
		mDevice.Get(),
		uavDesc,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_STATE_COMMON);

	// 2. Create UAV Descriptor
	auto uavHandle = mSrvHeap.Allocate();
	mUavSlot_Output = uavHandle.index;

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavViewDesc = {};
	uavViewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	uavViewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

	mDevice.Get()->CreateUnorderedAccessView(
		mComputeOutputTexture.Get(),
		nullptr,
		&uavViewDesc,
		uavHandle.cpuHandle);

	auto rtvHandle = mGBufferRtvHeap.Allocate();
	mRtvIndex_ComputeOutput = rtvHandle.index;

	mDevice.Get()->CreateRenderTargetView(
		mComputeOutputTexture.Get(),
		nullptr,
		rtvHandle.cpuHandle);

	// ====================================================================================
	// 2. Create Direct Lighting Intermediate Texture
	// This is where LightPassCS writes. We use FLOAT format to preserve HDR data.
	// ====================================================================================

	// We reuse the desc but change format to Float16 for HDR precision
	//auto lightingDesc = uavDesc;
	//lightingDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;

	//mDirectLightingTexture.Initialize(
	//	mDevice.Get(),
	//	lightingDesc,
	//	D3D12_HEAP_TYPE_DEFAULT,
	//	D3D12_RESOURCE_STATE_COMMON);

	//// A. Create UAV (For LightPassCS to WRITE to u0)
	//auto uavDirectHandle = mSrvHeap.Allocate();
	//mUavSlot_DirectLighting = uavDirectHandle.index;

	//D3D12_UNORDERED_ACCESS_VIEW_DESC lightingUavViewDesc = {};
	//lightingUavViewDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	//lightingUavViewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

	//mDevice.Get()->CreateUnorderedAccessView(
	//	mDirectLightingTexture.Get(),
	//	nullptr,
	//	&lightingUavViewDesc,
	//	uavDirectHandle.cpuHandle);

	//// B. Create SRV (For CompositeCS to READ from t0)
	//auto srvDirectHandle = mSrvHeap.Allocate();
	//mSrvSlot_DirectLighting = srvDirectHandle.index;

	//D3D12_SHADER_RESOURCE_VIEW_DESC lightingSrvDesc = {};
	//lightingSrvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	//lightingSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	//lightingSrvDesc.Texture2D.MipLevels = 1;
	//lightingSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	//mDevice.Get()->CreateShaderResourceView(
	//	mDirectLightingTexture.Get(),
	//	&lightingSrvDesc,
	//	srvDirectHandle.cpuHandle);
}

void Renderer::CreateLightBuffer()
{
	UINT stride = sizeof(LightData);
	UINT size = stride * cMaxLights;

	mGlobalLightBuffer.Initialize(
		mDevice.Get(),
		size,
		D3D12_HEAP_TYPE_UPLOAD,
		D3D12_RESOURCE_STATE_GENERIC_READ);

	// Create SRV
	auto srvHandle = mSrvHeap.Allocate();
	mSrvSlot_LightBuffer = srvHandle.index;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_UNKNOWN;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	srvDesc.Buffer.FirstElement = 0;
	srvDesc.Buffer.NumElements = cMaxLights;
	srvDesc.Buffer.StructureByteStride = stride;
	srvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;

	mDevice.Get()->CreateShaderResourceView(
		mGlobalLightBuffer.Get(),
		&srvDesc,
		srvHandle.cpuHandle);
}

void Renderer::InitializeReflectionResources()
{
	// 1. Create Reflection Texture (Same size as screen, RGBA16F for HDR)
	auto desc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R16G16B16A16_FLOAT, // High precision for reflections
		mWidth, mHeight, 1, 1, 1, 0,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
	);

	mOutDiffuseTex.Initialize(
		mDevice.Get(),
		desc,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_STATE_COMMON);
	mOutSpecularTex.Initialize(
		mDevice.Get(),
		desc,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_STATE_COMMON);

	mOutDiffuseTex.Get()->SetName(L"Out Diffuse Texture");
	mOutSpecularTex.Get()->SetName(L"Out Specular Texture");


	// 2. Create UAV (For writing in DXR)
	auto uavAlloc = mSrvHeap.Allocate(2);
	mUavSlot_Diffuse = uavAlloc.index;
	mUavSlot_Specular = uavAlloc.index + 1;

	auto diffuseUavHandle = uavAlloc.cpuHandle;
	auto specularUavHandle = mSrvHeap.GetCpuHandle(mUavSlot_Specular);

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	mDevice.Get()->CreateUnorderedAccessView(mOutDiffuseTex.Get(), nullptr, &uavDesc, diffuseUavHandle);
	mDevice.Get()->CreateUnorderedAccessView(mOutSpecularTex.Get(), nullptr, &uavDesc, specularUavHandle);


	// 3. Create SRV (For reading in Composite Pass)
	auto srvAlloc = mSrvHeap.Allocate(2);
	mSrvSlot_Diffuse = srvAlloc.index;
	mSrvSlot_Specular = srvAlloc.index + 1;

	auto diffuseSrvHandle = srvAlloc.cpuHandle;
	auto specularSrvHandle = mSrvHeap.GetCpuHandle(mSrvSlot_Specular);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	mDevice.Get()->CreateShaderResourceView(mOutDiffuseTex.Get(), &srvDesc, diffuseSrvHandle);
	mDevice.Get()->CreateShaderResourceView(mOutSpecularTex.Get(), &srvDesc, specularSrvHandle);
}


void Renderer::InitializeRootSignatures()
{
	mMeshRootSignature.InitializeMeshRS(mDevice.Get());
	mComputeRootSignature.InitializeComputeRS(mDevice.Get());
	mCompositeRootSignature.InitializeCompositeRS(mDevice.Get());
}

void Renderer::InitializePipelineState()
{
	HLSLShader vertexShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/VertexShader.hlsl", L"vs_6_0");
	HLSLShader pixelShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/PixelShader.hlsl", L"ps_6_0");
	HLSLShader computeShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/LightPassCS.hlsl", L"cs_6_5");
	HLSLShader transparentPixelShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/TransparentPixelShader.hlsl", L"ps_6_0");
	HLSLShader compositeShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/CompositeCS.hlsl", L"cs_6_0");

	std::vector<ShaderMacro> maskedDefines = {
		{ L"ALPHA_TEST", L"1" }
	};
	HLSLShader maskedPixelShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/PixelShader.hlsl", L"ps_6_0", maskedDefines);

	constexpr D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc
	{
		.pInputElementDescs = inputElementDescs,
		.NumElements = _countof(inputElementDescs),
	};

	mCommandQueue.Flush();

	// 1. Opaque pipeline state
	mPipelineStateOpaqueSingle.InitializeOpaque(
		mDevice.Get(),
		mMeshRootSignature.Get(),
		vertexShader,
		pixelShader,
		inputLayoutDesc);

	// 2. Masked pipeline state (like opaque but with clip)
	mPipelineStateMaskedSingle.InitializeOpaque(
		mDevice.Get(),
		mMeshRootSignature.Get(),
		vertexShader,
		maskedPixelShader,
		inputLayoutDesc);

	// 3. Transparent pipeline state
	mPipelineStateTransparent.InitializeTransparent(
		mDevice.Get(),
		mMeshRootSignature.Get(),
		vertexShader,
		std::move(transparentPixelShader),
		inputLayoutDesc);

	// 4. Opaque double-sided pipeline state
	mPipelineStateOpaqueDouble.InitializeOpaque(
		mDevice.Get(),
		mMeshRootSignature.Get(),
		vertexShader,
		std::move(pixelShader),
		inputLayoutDesc,
		true);

	// 5. Masked double-sided pipeline state
	mPipelineStateMaskedDouble.InitializeOpaque(
		mDevice.Get(),
		mMeshRootSignature.Get(),
		std::move(vertexShader),
		std::move(maskedPixelShader),
		inputLayoutDesc,
		true);

	// 6. Compute pipeline state for deferred
	mPipelineStateCompute.InitializeCompute(
		mDevice.Get(),
		mComputeRootSignature.Get(),
		std::move(computeShader));

	// 7. Composite pipeline state for merging deferred render with reflections
	mPipelineStateComposite.InitializeCompute(
		mDevice.Get(),
		mCompositeRootSignature.Get(),
		std::move(compositeShader));
}

void Renderer::InitializeTextureLoader()
{
	HLSLShader mipmapShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/MipmapShader.hlsl", L"cs_6_0");
	mTextureLoader.Initialize(mDevice.Get(), &mSrvHeap, &mCommandQueue, &mCommandList, &mUploadHeap, std::move(mipmapShader));
}

void Renderer::Update(const DirectX::XMMATRIX& viewProj, const DirectX::XMFLOAT3& cameraPos, const DirectX::XMFLOAT3& cameraForward)
{
	// compute delta time
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	double dt = static_cast<double>(now.QuadPart - mPrevCounter.QuadPart) * mSecondsPerCount;
	mPrevCounter = now;

	size_t cbOffset;
	if (mSceneLoaded)
	{
		mConstantBufferData.vpMatrix = viewProj;
		mConstantBufferData.InvVpMatrix = DirectX::XMMatrixInverse(nullptr, viewProj);
		mConstantBufferData.viewPos = cameraPos;
		mConstantBufferData.frameCount = mFrameCount++;

		UINT currentBackBufferIndex = mSwapChain.GetCurrentBackBufferIndex();
		size_t alignedSize = (sizeof(ConstantBufferData) + 255) & ~255; // Align to 256 bytes
		cbOffset = alignedSize * currentBackBufferIndex;

		void* pData;
		mConstantBuffer.Get()->Map(0, nullptr, &pData);
		uint8_t* pByteData = reinterpret_cast<uint8_t*>(pData);
		memcpy(pByteData + cbOffset, &mConstantBufferData, sizeof(ConstantBufferData));
		mConstantBuffer.Get()->Unmap(0, nullptr);

		// Sort transparent meshes back-to-front each frame (temporary solution)
		SortTransparentMeshes(cameraPos);
	}

	// Wait for GPU to finish with the current back buffer
	mCommandQueue.WaitForFenceInFrame(mSwapChain.GetCurrentBackBufferIndex());
	// Open command list
	mCommandList.ResetCommandList(mSwapChain.GetCurrentBackBufferIndex());

	if (mSceneLoaded)
	{
		// Bind descriptor heap
		ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
		mCommandList.Get()->SetDescriptorHeaps(_countof(heaps), heaps);

		// =========================================================================================
		// STAGE 1: G-BUFFER PASS (Rasterization)
		// =========================================================================================
		{
			// A. Transition G-Buffer Resources to RENDER_TARGET
			D3D12_RESOURCE_BARRIER barriers[]
			{
				CD3DX12_RESOURCE_BARRIER::Transition(mGBufferAlbedo.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET),
				CD3DX12_RESOURCE_BARRIER::Transition(mGBufferNormal.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET),
				CD3DX12_RESOURCE_BARRIER::Transition(mGBufferMaterial.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET),
				CD3DX12_RESOURCE_BARRIER::Transition(mDepthBuffer.GetResource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE),
				CD3DX12_RESOURCE_BARRIER::Transition(mGBufferEmission.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET),
			};
			mCommandList.Get()->ResourceBarrier(_countof(barriers), barriers);

			// B. Clear Targets
			D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[4] = {
				mGBufferRtvHeap.GetCpuHandle(0),
				mGBufferRtvHeap.GetCpuHandle(1),
				mGBufferRtvHeap.GetCpuHandle(2),
				mGBufferRtvHeap.GetCpuHandle(3)
			};

			float clearColorBlack[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			float clearColorNormal[4] = { 0.5f, 0.5f, 1.0f, 1.0f };
			float clearColorMaterial[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
			float clearDepth = 1.0f;
			mCommandList.Get()->ClearRenderTargetView(rtvHandles[0], clearColorBlack, 0, nullptr);
			mCommandList.Get()->ClearRenderTargetView(rtvHandles[1], clearColorNormal, 0, nullptr);
			mCommandList.Get()->ClearRenderTargetView(rtvHandles[2], clearColorMaterial, 0, nullptr);
			mCommandList.Get()->ClearRenderTargetView(rtvHandles[3], clearColorBlack, 0, nullptr);
			mCommandList.Get()->ClearDepthStencilView(mDepthBuffer.GetDSVHandle(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

			// C. Set Render Targets
			auto dsvHandle = mDepthBuffer.GetDSVHandle();
			mCommandList.Get()->OMSetRenderTargets(4, rtvHandles, FALSE, &dsvHandle);
			mCommandList.Get()->RSSetViewports(1, &mViewport);
			mCommandList.Get()->RSSetScissorRects(1, &mScissorRect);

			// D. Draw Opaque & Masked Geometry
			mCommandList.Get()->SetGraphicsRootSignature(mMeshRootSignature.Get());
			mCommandList.Get()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			mCommandList.Get()->SetGraphicsRootConstantBufferView(0, mConstantBuffer.Get()->GetGPUVirtualAddress() + cbOffset);

			mCommandList.Get()->SetPipelineState(mPipelineStateOpaqueSingle.Get());
			for (const auto& mesh : mOpaqueSingleSidedMeshes)
				DrawMesh(mesh);
			mCommandList.Get()->SetPipelineState(mPipelineStateOpaqueDouble.Get());
			for (const auto& mesh : mOpaqueDoubleSidedMeshes)
				DrawMesh(mesh);
			mCommandList.Get()->SetPipelineState(mPipelineStateMaskedSingle.Get());
			for (const auto& mesh : mMaskedSingleMeshes)
				DrawMesh(mesh);
			mCommandList.Get()->SetPipelineState(mPipelineStateMaskedDouble.Get());
			for (const auto& mesh : mMaskedDoubleSidedMeshes)
				DrawMesh(mesh);
			//mCommandList.Get()->SetPipelineState(mPipelineStateTransparent.Get());
			//for (const auto& mesh : mTransparentMeshes)
			//	DrawMesh(mesh);
		}

		// =========================================================================================
		// STAGE 2: LIGHTING PASS (Compute Shader + Inline Ray Tracing)
		// =========================================================================================
		{
			// A. Transition Resources
			// G-Buffer -> Shader Resource (Read)
			// Depth -> Shader Resource (Read)
			// Output Texture -> Unordered Access (Write)
			D3D12_RESOURCE_BARRIER barriers[]
			{
				CD3DX12_RESOURCE_BARRIER::Transition(mGBufferAlbedo.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mGBufferNormal.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mGBufferMaterial.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mDepthBuffer.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				//CD3DX12_RESOURCE_BARRIER::Transition(mDirectLightingTexture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
				CD3DX12_RESOURCE_BARRIER::Transition(mGBufferEmission.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			};
			mCommandList.Get()->ResourceBarrier(_countof(barriers), barriers);

			// B. Bind Compute Pipeline
			mCommandList.Get()->SetComputeRootSignature(mComputeRootSignature.Get());
			mCommandList.Get()->SetPipelineState(mPipelineStateCompute.Get());

			// C. Bind Resources (Slots based on computeSigDesc)
			// Slot 0: CBV (Frame Data)
			mCommandList.Get()->SetComputeRootConstantBufferView(0, mConstantBuffer.Get()->GetGPUVirtualAddress() + cbOffset);

			// Slot 1: G-Buffer SRV Table (t0 - t4)
			// We use the wrapper to get the GPU handle of the *start* of the G-Buffer allocation
			mCommandList.Get()->SetComputeRootDescriptorTable(1, mSrvHeap.GetGpuHandle(mSrvSlot_GBufferAlbedo));

			// Slot 2: TLAS SRV (t5) - Raw Address
			mCommandList.Get()->SetComputeRootShaderResourceView(2, mTLAS.Get()->GetGPUVirtualAddress());

			// Slot 3: Light Buffer SRV (t6) - Raw Address
			// Ensure mLightBuffer is created!
			mCommandList.Get()->SetComputeRootShaderResourceView(3, mGlobalLightBuffer.Get()->GetGPUVirtualAddress());

			// Slot 4: Output UAV Table (u0)
			//mCommandList.Get()->SetComputeRootDescriptorTable(4, mSrvHeap.GetGpuHandle(mUavSlot_DirectLighting));

			// D. Dispatch
			// Threads (8, 8, 1). Dispatch (Width/8, Height/8, 1)
			//mCommandList.Get()->Dispatch((mWidth + 7) / 8, (mHeight + 7) / 8, 1);
		}

		// =========================================================================
		// STAGE 3: REFLECTIONS (DXR Pipeline)
		// =========================================================================
		{
			// 1. Barrier: Output needs to be UAV
			D3D12_RESOURCE_BARRIER barriers[]
			{
				CD3DX12_RESOURCE_BARRIER::Transition(mOutDiffuseTex.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				CD3DX12_RESOURCE_BARRIER::Transition(mOutSpecularTex.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
			};
			mCommandList.Get()->ResourceBarrier(_countof(barriers), barriers);

			// 2. Dispatch Rays
			// Note: The Global Root Sig needs binding just like a Compute Shader
			mCommandList.Get()->SetComputeRootSignature(mComputeRootSignature.Get());
			mCommandList.Get()->SetComputeRootConstantBufferView(0, mConstantBuffer.Get()->GetGPUVirtualAddress() + cbOffset);
			mCommandList.Get()->SetComputeRootDescriptorTable(1, mSrvHeap.GetGpuHandle(mSrvSlot_GBufferAlbedo)); // G-Buffer
			mCommandList.Get()->SetComputeRootShaderResourceView(2, mTLAS.Get()->GetGPUVirtualAddress());
			mCommandList.Get()->SetComputeRootShaderResourceView(3, mGlobalLightBuffer.Get()->GetGPUVirtualAddress());

			// Bind Reflection Output UAV (Slot u0 in Reflections.hlsl)
			mCommandList.Get()->SetComputeRootDescriptorTable(4, mSrvHeap.GetGpuHandle(mUavSlot_Diffuse));

			mReflectionsPipeline.Dispatch(mCommandList.Get(), mWidth, mHeight);
		}

		// =========================================================================
		// STAGE 4: COMPOSITE (Merge Direct + Reflection)
		// =========================================================================
		{
			// Barriers: Reflection -> Read, DirectLight -> Read, Output -> Write
			// (For simplicity, let's say we write back into mComputeOutputTexture)
			D3D12_RESOURCE_BARRIER barriers[]
			{
				CD3DX12_RESOURCE_BARRIER::Transition(mOutDiffuseTex.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mOutSpecularTex.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mComputeOutputTexture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
			};
			mCommandList.Get()->ResourceBarrier(_countof(barriers), barriers);

			// Bind Composite Pipeline
			mCommandList.Get()->SetComputeRootSignature(mCompositeRootSignature.Get());
			mCommandList.Get()->SetPipelineState(mPipelineStateComposite.Get());

			// Bind Descriptors
			// Slot 0: CBV
			mCommandList.Get()->SetComputeRootConstantBufferView(0, mConstantBuffer.Get()->GetGPUVirtualAddress() + cbOffset);

			// Slot 1: Direct Lighting Table (t0)
			mCommandList.Get()->SetComputeRootDescriptorTable(1, mSrvHeap.GetGpuHandle(mSrvSlot_Diffuse));

			// Slot 1: Reflection SRV (t1)
			mCommandList.Get()->SetComputeRootDescriptorTable(2, mSrvHeap.GetGpuHandle(mSrvSlot_Specular));

			// Slot 2: G-Buffer Table (t2 - t5)
			mCommandList.Get()->SetComputeRootDescriptorTable(3, mSrvHeap.GetGpuHandle(mSrvSlot_GBufferAlbedo));

			// Slot 3: Output UAV (u0)
			mCommandList.Get()->SetComputeRootDescriptorTable(4, mSrvHeap.GetGpuHandle(mUavSlot_Output));

			// Dispatch
			mCommandList.Get()->Dispatch((mWidth + 7) / 8, (mHeight + 7) / 8, 1);

			D3D12_RESOURCE_BARRIER cleanup[]
			{
				CD3DX12_RESOURCE_BARRIER::Transition(mOutDiffuseTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
				CD3DX12_RESOURCE_BARRIER::Transition(mOutSpecularTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
			};

			mCommandList.Get()->ResourceBarrier(_countof(cleanup), cleanup);
		}

		// =========================================================================================
		// STAGE 5: TRANSPARENT FORWARD PASS
		// =========================================================================================
		{
			D3D12_RESOURCE_BARRIER barriers[]
			{
				// 1. Transition Output Texture: UAV (from Compute) -> RENDER_TARGET
				CD3DX12_RESOURCE_BARRIER::Transition(mComputeOutputTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET),
				// 2. Transition Depth: SHADER_RESOURCE (from Compute) -> DEPTH_READ
				// We need to READ depth to occlude glass behind walls, but we don't need to WRITE (usually).
				CD3DX12_RESOURCE_BARRIER::Transition(mDepthBuffer.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_READ),
			};
			mCommandList.Get()->ResourceBarrier(2, barriers);

			// Bind Targets
			// We write Color to ComputeOutput, and Read Depth from DepthBuffer
			//D3D12_CPU_DESCRIPTOR_HANDLE rtv = mGBufferRtvHeap.GetCpuHandle(mRtvIndex_ComputeOutput);
			//D3D12_CPU_DESCRIPTOR_HANDLE dsv = mDepthBuffer.GetDSVHandle();

			//mCommandList.Get()->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
			//mCommandList.Get()->RSSetViewports(1, &mViewport);
			//mCommandList.Get()->RSSetScissorRects(1, &mScissorRect);

			//// Draw Transparent Meshes
			//mCommandList.Get()->SetGraphicsRootSignature(mMeshRootSignature.Get());
			//mCommandList.Get()->SetPipelineState(mPipelineStateTransparent.Get());
			//mCommandList.Get()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
			//mCommandList.Get()->SetGraphicsRootConstantBufferView(0, mConstantBuffer.Get()->GetGPUVirtualAddress());

			//for (const auto& mesh : mTransparentMeshes)
			//    DrawMesh(mesh);
		}



		// =========================================================================================
		// STAGE 6: COPY TO BACKBUFFER
		// =========================================================================================
		{
			D3D12_RESOURCE_BARRIER barriers[]
			{
				// Transition Output Texture -> Copy Source
				CD3DX12_RESOURCE_BARRIER::Transition(mComputeOutputTexture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COPY_SOURCE),
				// Transition Back Buffer -> Copy Dest
				CD3DX12_RESOURCE_BARRIER::Transition(mSwapChain.GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST),
				// Transition Depth Buffer back to Common
				CD3DX12_RESOURCE_BARRIER::Transition(mDepthBuffer.GetResource(), D3D12_RESOURCE_STATE_DEPTH_READ, D3D12_RESOURCE_STATE_COMMON),
				// Transition Emissive G-Buffer back to Common
				CD3DX12_RESOURCE_BARRIER::Transition(mGBufferEmission.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
			};

			mCommandList.Get()->ResourceBarrier(_countof(barriers), barriers);

			// Copy
			mCommandList.Get()->CopyResource(
				mSwapChain.GetCurrentBackBuffer(),
				mComputeOutputTexture.Get());

			// Transition Back Buffer -> Present
			D3D12_RESOURCE_BARRIER renderTargetBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
				mSwapChain.GetCurrentBackBuffer(),
				D3D12_RESOURCE_STATE_COPY_DEST,
				D3D12_RESOURCE_STATE_RENDER_TARGET);
			mCommandList.Get()->ResourceBarrier(1, &renderTargetBarrier);

			// Cleanup G-Buffer Transitions (Back to Common)
			D3D12_RESOURCE_BARRIER cleanup[]
			{
				CD3DX12_RESOURCE_BARRIER::Transition(mGBufferAlbedo.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
				CD3DX12_RESOURCE_BARRIER::Transition(mGBufferNormal.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
				CD3DX12_RESOURCE_BARRIER::Transition(mGBufferMaterial.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
				CD3DX12_RESOURCE_BARRIER::Transition(mComputeOutputTexture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
			};
			mCommandList.Get()->ResourceBarrier(_countof(cleanup), cleanup);
		}
	}
	else
	{
		D3D12_RESOURCE_BARRIER renderTargetBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
			mSwapChain.GetCurrentBackBuffer(),
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET);
		mCommandList.Get()->ResourceBarrier(1, &renderTargetBarrier);


		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = mSwapChain.GetCurrentBackBufferView();

		// 3. [FIX] Clear and Bind
		float clearColor[4] = { 0.1f, 0.1f, 0.1f, 1.0f }; // Dark gray background
		mCommandList.Get()->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	}

	if (ImGui::GetIO().BackendRendererUserData != nullptr)
	{
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = mSwapChain.GetCurrentBackBufferView();
		mCommandList.Get()->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);
		RenderImGui();
	}
	D3D12_RESOURCE_BARRIER presentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
		mSwapChain.GetCurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_PRESENT);
	mCommandList.Get()->ResourceBarrier(1, &presentBarrier);


	// Execute command list
	mCommandList.Get()->Close();
	ID3D12CommandList* commandLists[] = { mCommandList.Get() };
	mCommandQueue.ExecuteCommandLists(1, commandLists);

	// Present the frame
	mSwapChain.Present();

	// Signal and increment the fence value
	mCommandQueue.SignalFenceInFrame(mSwapChain.GetCurrentBackBufferIndex());
}

void Renderer::DrawMesh(const MeshGpuData& mesh)
{
    mCommandList.Get()->IASetVertexBuffers(0, 1, &mesh.vbv);
    mCommandList.Get()->IASetIndexBuffer(&mesh.ibv);
    mCommandList.Get()->SetGraphicsRootDescriptorTable(1, mesh.materialTable.gpuHandle);
	mCommandList.Get()->SetGraphicsRoot32BitConstants(2, sizeof(MeshMaterialData) / 4, &mesh.materialData, 0);
    mCommandList.Get()->DrawIndexedInstanced(mesh.ibv.SizeInBytes / sizeof(UINT), 1, 0, 0, 0);
}

void Renderer::SortTransparentMeshes(const DirectX::XMFLOAT3& cameraPos)
{
    for (auto& mesh : mTransparentMeshes)
    {
        DirectX::XMVECTOR center = DirectX::XMLoadFloat3(&mesh.center);
        DirectX::XMVECTOR camPos = DirectX::XMLoadFloat3(&cameraPos);
        DirectX::XMVECTOR toCamera = DirectX::XMVectorSubtract(camPos, center);
        mesh.distanceToCamera = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(toCamera));
    }

    std::sort(mTransparentMeshes.begin(), mTransparentMeshes.end(),
        [](const MeshGpuData& a, const MeshGpuData& b)
        {
            return a.distanceToCamera > b.distanceToCamera;
        });
}

void Renderer::InitializeImGui(HWND hwnd)
{
    // Create ImGui context
    IMGUI_CHECKVERSION();
    mImGuiContext = ImGui::CreateContext();
    ImGui::SetCurrentContext(mImGuiContext);

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Configure font rendering for better quality
    ImFontConfig fontConfig;
    fontConfig.OversampleH = 3;  // Horizontal oversampling for sharper text
    fontConfig.OversampleV = 3;  // Vertical oversampling for sharper text
    fontConfig.PixelSnapH = false;  // Better subpixel rendering
    
    // Try to load Segoe UI font (Windows system font) for better quality
    ImFont* font = io.Fonts->AddFontFromFileTTF("C:/Windows/Fonts/segoeui.ttf", 17.0f, &fontConfig);
    
    // If Segoe UI fails to load, fall back to default font with high quality settings
    if (!font)
    {
        wcout << "Warning: Could not load Segoe UI font, using default ImGui font" << endl;
        io.Fonts->AddFontDefault(&fontConfig);
    }

    // Build font atlas with higher quality
    io.Fonts->Build();

    // Setup ImGui style
    ImGui::StyleColorsDark();
    
    // Adjust style for better text rendering
    ImGuiStyle& style = ImGui::GetStyle();
    style.AntiAliasedLines = true;
    style.AntiAliasedFill = true;
    style.AntiAliasedLinesUseTex = true;
    
    // Slightly adjust rounding for modern look
    style.WindowRounding = 6.0f;
    style.FrameRounding = 4.0f;
    style.GrabRounding = 4.0f;

    // Create descriptor heap for ImGui (1 descriptor for font texture)
	mImGuiSrvHeap.Initialize(mDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, 1, true);

    // Initialize Win32 backend first
    ImGui_ImplWin32_Init(hwnd);
    
    // Initialize DX12 backend
    ImGui_ImplDX12_Init(
        mDevice.Get(),
        Config::cFrameCount,
        Config::cBackBufferFormat,
        mImGuiSrvHeap.Get(),
        mImGuiSrvHeap.GetCpuHandle(0),
        mImGuiSrvHeap.GetGpuHandle(0)
    );

    // CRITICAL: Manually build and upload font atlas
    // Get font texture data
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    
    // Open command list for upload
    mCommandList.ResetCommandList(0);
    
    // Bind ImGui descriptor heap
    ID3D12DescriptorHeap* heaps[] = { mImGuiSrvHeap.Get() };
    mCommandList.Get()->SetDescriptorHeaps(_countof(heaps), heaps);
    
    // Create device objects (this uploads the font texture)
    ImGui_ImplDX12_CreateDeviceObjects();
    
    // Close and execute command list
    mCommandList.Get()->Close();
    ID3D12CommandList* lists[] = { mCommandList.Get() };
    mCommandQueue.ExecuteCommandLists(1, lists);
    mCommandQueue.Flush();
    
    wcout << "ImGui initialized: Font atlas " << width << "x" << height << " uploaded to GPU" << endl;
}

void Renderer::ShutdownImGui()
{
    if (mImGuiContext)
    {
        ImGui_ImplDX12_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext(mImGuiContext);
        mImGuiContext = nullptr;
    }
}

void Renderer::BeginImGuiFrame()
{
    ImGui::SetCurrentContext(mImGuiContext);
    
    // Correct order: DX12 backend first (builds font atlas), then Win32, then ImGui
    ImGui_ImplDX12_NewFrame();
    ImGui_ImplWin32_NewFrame();
    ImGui::NewFrame();
}

void Renderer::RenderImGui()
{
    ImGui::SetCurrentContext(mImGuiContext);
    ImGui::Render();

    // Bind ImGui descriptor heap
    ID3D12DescriptorHeap* imguiHeaps[] = { mImGuiSrvHeap.Get() };
    mCommandList.Get()->SetDescriptorHeaps(_countof(imguiHeaps), imguiHeaps);

    // Render ImGui draw data
    ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());
}

bool Renderer::AddExtensionScenes(std::vector<std::unique_ptr<Model>> models)
{
	if (models.empty())
		return false;

	wcout << L"Adding " << models.size() << L" extension scene(s)..." << endl;

	// Wait for GPU to finish all work before adding new scenes
	mCommandQueue.Flush();

	// Store the count of models before adding extensions
	size_t previousModelCount = mModels.size();
	size_t previousMeshCount = mOpaqueSingleSidedMeshes.size() + mOpaqueDoubleSidedMeshes.size() + 
	                           mMaskedSingleMeshes.size() + mMaskedDoubleSidedMeshes.size() + 
	                           mTransparentMeshes.size();
	
	// Try to load models one by one, catching any memory allocation failures
	size_t successfullyLoaded = 0;
	size_t totalAttempted = models.size();
	
	for (size_t i = 0; i < models.size(); ++i)
	{
		auto& model = models[i];
		if (!model)
			continue;

		try
		{
			// Check if we have meshes before trying to add
			if (model->mMeshes.empty())
			{
				wcout << L"Warning: Extension model " << (i + 1) << L" has no meshes, skipping." << endl;
				continue;
			}
			
			// Try to add the model
			mModels.push_back(std::move(model));
			successfullyLoaded++;
			
			wcout << L"Successfully added extension model " << successfullyLoaded << L" / " << totalAttempted << endl;
		}
		catch (const std::bad_alloc& e)
		{
			wcout << L"Memory allocation failed at extension model " << (i + 1) << L": " << e.what() << endl;
			wcout << L"Stopping further model loading due to memory constraints." << endl;
			break;
		}
		catch (const std::exception& e)
		{
			wcout << L"Error adding extension model " << (i + 1) << L": " << e.what() << endl;
			// Continue trying with next model
			continue;
		}
	}

	if (successfullyLoaded == 0)
	{
		wcout << L"Error: No extension models could be loaded." << endl;
		return false;
	}

	wcout << L"Added " << successfullyLoaded << L" / " << totalAttempted << L" extension models." << endl;

	// Try to build GPU data for all loaded models (including existing ones)
	try
	{
		std::chrono::steady_clock::time_point meshBuildStartTime = std::chrono::steady_clock::now();
		BuildMeshGpuData();
		std::chrono::steady_clock::time_point meshBuildEndTime = std::chrono::steady_clock::now();
		std::chrono::duration<double> elapsedSeconds = meshBuildEndTime - meshBuildStartTime;
		wcout << "Mesh GPU data rebuilt in " << elapsedSeconds.count() << " seconds." << endl;
	}
	catch (const std::runtime_error& e)
	{
		wcout << L"GPU memory exhausted during mesh data upload: " << e.what() << endl;
		wcout << L"Extension models could not be fully loaded. Reverting to previous state." << endl;
		
		// Remove the extension models that we just added
		mModels.resize(previousModelCount);
		
		// Rebuild GPU data with just the original models
		try
		{
			BuildMeshGpuData();
		}
		catch (...)
		{
			wcout << L"Critical error: Failed to restore previous state!" << endl;
		}
		
		return false;
	}
	catch (const std::exception& e)
	{
		wcout << L"Error building mesh GPU data: " << e.what() << endl;
		
		// Remove the extension models and try to restore previous state
		mModels.resize(previousModelCount);
		
		try
		{
			BuildMeshGpuData();
		}
		catch (...)
		{
			wcout << L"Critical error: Failed to restore previous state!" << endl;
		}
		
		return false;
	}
	
	// Try to build ray tracing structures
	try
	{
		std::chrono::steady_clock::time_point rtBuildStartTime = std::chrono::steady_clock::now();
		InitializeRayTracing();
		std::chrono::steady_clock::time_point rtBuildEndTime = std::chrono::steady_clock::now();
		std::chrono::duration<double> rtElapsedSeconds = rtBuildEndTime - rtBuildStartTime;
		wcout << "Ray tracing structures rebuilt in " << rtElapsedSeconds.count() << " seconds." << endl;
	}
	catch (const std::exception& e)
	{
		wcout << L"Warning: Failed to rebuild ray tracing structures: " << e.what() << endl;
		// Non-critical, continue without RT acceleration
	}

	// Collect lights (this should be safe)
	try
	{
		CollectStaticLights();
	}
	catch (const std::exception& e)
	{
		wcout << L"Warning: Failed to collect lights: " << e.what() << endl;
	}

	size_t newMeshCount = mOpaqueSingleSidedMeshes.size() + mOpaqueDoubleSidedMeshes.size() + 
	                      mMaskedSingleMeshes.size() + mMaskedDoubleSidedMeshes.size() + 
	                      mTransparentMeshes.size();
	size_t addedMeshCount = newMeshCount - previousMeshCount;
	
	wcout << L"Extension scene(s) added successfully! (Total: " << mModels.size() << L" model(s), " 
	      << newMeshCount << L" meshes, added: " << addedMeshCount << L" meshes)" << endl;
	return true;
}
