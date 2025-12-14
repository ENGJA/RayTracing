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
    const int staticCount = static_cast<int>(std::min<size_t>(mStaticLights.size(), cMaxLights));
    for (int i = 0; i < staticCount; ++i)
    {
        mConstantBufferData.lights[i] = mStaticLights[i];
    }
    // Set numLights to static count for now; Update will adjust (append camera light) each frame if needed.
    mConstantBufferData.numLights = staticCount;
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
    D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
    dsvDesc.Format = Config::cDepthBufferFormat;
    dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

    mDevice.Get()->CreateDepthStencilView(
        mDepthBuffer.GetResource(),
        &dsvDesc,
        mDepthBuffer.GetDSVHandle()
    );

    mWidth = width;
    mHeight = height;

    // setup timer for delta time
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);
    mSecondsPerCount = 1.0 / static_cast<double>(freq.QuadPart);
    QueryPerformanceCounter(&mPrevCounter);

    // Shader-visible SRV heap for textures (increase capacity for many material descriptors)
    mSrvHeap.Initialize(mDevice.Get(), Config::cNumberOfSrvDescriptors);

    // Shared upload heap (64 MB)
    mUploadHeap.Initialize(mDevice.Get(), 512ull * 1024ull * 1024ull);

	mShaderCompiler.Initialize();

    InitializeTextureLoader();

    InitializePipelineState();

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

    mConstantBuffer.Initialize(
        mDevice.Get(),
        sizeof(ConstantBufferData),
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

	// Wait for GPU to complete all work
	mCommandQueue.Flush();

	// Update dimensions
	mWidth = width;
	mHeight = height;

	// Resize swap chain buffers
	mSwapChain.Resize(width, height);

	// Recreate depth buffer with new dimensions
	mDepthBuffer.Initialize(mDevice.Get(), width, height);
	D3D12_DEPTH_STENCIL_VIEW_DESC dsvDesc{};
	dsvDesc.Format = Config::cDepthBufferFormat;
	dsvDesc.ViewDimension = D3D12_DSV_DIMENSION_TEXTURE2D;

	mDevice.Get()->CreateDepthStencilView(
		mDepthBuffer.GetResource(),
		&dsvDesc,
		mDepthBuffer.GetDSVHandle()
	);

	// Update viewport
	mViewport.TopLeftX = 0.0f;
	mViewport.TopLeftY = 0.0f;
	mViewport.Width = static_cast<FLOAT>(width);
	mViewport.Height = static_cast<FLOAT>(height);
	mViewport.MinDepth = 0.0f;
	mViewport.MaxDepth = 1.0f;

	// Update scissor rect
	mScissorRect.left = 0;
	mScissorRect.top = 0;
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
}

void Renderer::InitializePipelineState()
{
    HLSLShader vertexShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/VertexShader.hlsl", L"vs_6_0");
    HLSLShader pixelShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/PixelShader.hlsl", L"ps_6_0");

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
        vertexShader,
        pixelShader,
        inputLayoutDesc);

	// 2. Masked pipeline state (like opaque but with clip)
    mPipelineStateMaskedSingle.InitializeOpaque(
        mDevice.Get(),
        vertexShader,
        maskedPixelShader,
		inputLayoutDesc);

	// 3. Transparent pipeline state
    mPipelineStateTransparent.InitializeTransparent(
        mDevice.Get(),
        vertexShader,
        pixelShader,
		inputLayoutDesc);

	// 4. Opaque double-sided pipeline state
    mPipelineStateOpaqueDouble.InitializeOpaque(
        mDevice.Get(),
        vertexShader,
        pixelShader,
		inputLayoutDesc,
		true);

    // 5. Masked double-sided pipeline state
    mPipelineStateMaskedDouble.InitializeOpaque(
        mDevice.Get(),
        vertexShader,
		std::move(maskedPixelShader),
		inputLayoutDesc,
        true);

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

    mConstantBufferData.vpMatrix = viewProj;
    mConstantBufferData.viewPos = DirectX::XMFLOAT4(cameraPos.x, cameraPos.y, cameraPos.z, 1.0f);

    // --- use cached static lights, avoid re-scanning models each frame ---
    const int staticCount = static_cast<int>(std::min<size_t>(mStaticLights.size(), cMaxLights));

    // light camera light
    const float cameraLightIntensity = 0.15f;
    if (staticCount < cMaxLights)
    {
        LightData camLight{};
        camLight.position = DirectX::XMFLOAT4(
            cameraPos.x + cameraForward.x * 1000.0f,
            cameraPos.y + cameraForward.y * 1000.0f,
            cameraPos.z + cameraForward.z * 1000.0f,
            1.0f);
        camLight.diffuseColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, cameraLightIntensity);
		camLight.specularColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, cameraLightIntensity);
        camLight.dirType = DirectX::XMFLOAT4(cameraForward.x, cameraForward.y, cameraForward.z, 1.0f); // directional flag
        mConstantBufferData.lights[staticCount] = camLight;
        mConstantBufferData.numLights = static_cast<int>(staticCount + 1);
    }
    else
    {
        // static lights already fill the limit; do not append camera light
        mConstantBufferData.numLights = staticCount;
    }

    void* pData;
    mConstantBuffer.Get()->Map(0, nullptr, &pData);
    memcpy(pData, &mConstantBufferData, sizeof(ConstantBufferData));
    mConstantBuffer.Get()->Unmap(0, nullptr);

	// Sort transparent meshes back-to-front each frame (temporary solution)
	SortTransparentMeshes(cameraPos);

    // Wait for GPU to finish with the current back buffer
    mCommandQueue.WaitForFenceInFrame(mSwapChain.GetCurrentBackBufferIndex());

    // Open command list
    mCommandList.ResetCommandList(mSwapChain.GetCurrentBackBufferIndex());

    // Bind descriptor heap
    ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
    mCommandList.Get()->SetDescriptorHeaps(_countof(heaps), heaps);

    // Transition back buffer to render target
    D3D12_RESOURCE_BARRIER barrier{};
    barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
    barrier.Transition.pResource = mSwapChain.GetCurrentBackBuffer();
    barrier.Transition.Subresource = 0;
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
    mCommandList.Get()->ResourceBarrier(1, &barrier);

    const FLOAT clearColor[4] = { 0.1f, 0.1f, 0.15f, 1.0f }; // Dark blue background

    D3D12_CPU_DESCRIPTOR_HANDLE rtvHandle = mSwapChain.GetCurrentBackBufferView();
    D3D12_CPU_DESCRIPTOR_HANDLE dsvHandle = mDepthBuffer.GetDSVHandle();

    mCommandList.Get()->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    mCommandList.Get()->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

    mCommandList.Get()->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
    mCommandList.Get()->RSSetViewports(1, &mViewport);
    mCommandList.Get()->RSSetScissorRects(1, &mScissorRect);

    mCommandList.Get()->SetGraphicsRootSignature(mPipelineStateOpaqueSingle.GetRootSignature());
    mCommandList.Get()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList.Get()->SetGraphicsRootConstantBufferView(0, mConstantBuffer.Get()->GetGPUVirtualAddress());

	// 1. Opaque single-sided
    mCommandList.Get()->SetPipelineState(mPipelineStateOpaqueSingle.Get());
    for (const auto& mesh : mOpaqueSingleSidedMeshes)
        DrawMesh(mesh);

	// 2. Opaque double-sided
    mCommandList.Get()->SetPipelineState(mPipelineStateOpaqueDouble.Get());
    for (const auto& mesh : mOpaqueDoubleSidedMeshes)
		DrawMesh(mesh);

	// 3. Masked single-sided
    mCommandList.Get()->SetPipelineState(mPipelineStateMaskedSingle.Get());
    for (const auto& mesh : mMaskedSingleMeshes)
		DrawMesh(mesh);

	// 4. Masked double-sided
	mCommandList.Get()->SetPipelineState(mPipelineStateMaskedDouble.Get());
	for (const auto& mesh : mMaskedDoubleSidedMeshes)
		DrawMesh(mesh);

	// 5. Transparent
	mCommandList.Get()->SetPipelineState(mPipelineStateTransparent.Get());
	for (const auto& mesh : mTransparentMeshes)
		DrawMesh(mesh);

    // Render ImGui if a frame was started
    ImGuiIO& io = ImGui::GetIO();
    if (io.BackendRendererUserData != nullptr) // Check if ImGui frame is active
    {
        RenderImGui();
    }

    // Transition back buffer to present
    barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
    mCommandList.Get()->ResourceBarrier(1, &barrier);

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
    mImGuiSrvHeap.Initialize(mDevice.Get(), 1);

    // Initialize Win32 backend first
    ImGui_ImplWin32_Init(hwnd);
    
    // Initialize DX12 backend
    ImGui_ImplDX12_Init(
        mDevice.Get(),
        Config::cFrameCount,
        Config::cBackBufferFormat,
        mImGuiSrvHeap.Get(),
        mImGuiSrvHeap.GetCPUHandle(0),
        mImGuiSrvHeap.GetGPUHandle(0)
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
