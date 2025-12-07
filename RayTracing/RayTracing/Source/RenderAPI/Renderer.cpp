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
    mCommandList.ResetCommandList(mSwapChain.GetCurrentBackBufferIndex());

    auto executeBatch = [this]()
    {
        mCommandList.Get()->Close();
        ID3D12CommandList* lists[] = { mCommandList.Get() };
        mCommandQueue.ExecuteCommandLists(1, lists);
        mCommandQueue.Flush();

        mUploadHeap.Reset();
        mCommandList.ResetCommandList(mSwapChain.GetCurrentBackBufferIndex());
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
		sunLight.dirType = DirectX::XMFLOAT4(0.5f, -1.0f, 0.5f, 1.0f); // directional light
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

    
    //const std::string modelPath = GetResourcePath("Objects\\sponza\\NewSponza_Main_glTF_003.gltf").string(); 
    //const std::string modelPath = R"(C:\Users\adria\Source\glTF-Sample-Assets\Models\ABeautifulGame\glTF\ABeautifulGame.gltf)";
    const std::string modelPath = R"(C:\Users\adria\Source\glTF-Sample-Assets\Models\AlphaBlendModeTest\glTF\AlphaBlendModeTest.gltf)";
    auto modelA = std::make_unique<Model>();

	std::chrono::steady_clock::time_point loadStartTime = std::chrono::steady_clock::now();
    modelA->loadModel(modelPath);
	std::chrono::steady_clock::time_point loadEndTime = std::chrono::steady_clock::now();
	std::chrono::duration<double> loadElapsedSeconds = loadEndTime - loadStartTime;
    if (modelA->mMeshes.empty())
    {
        vector<::Vertex> cpuVerts = {
            { { -1, -1, 0 }, {0,0,1}, {0,1} },
            { { -1,  1, 0 }, {0,0,1}, {0,0} },
            { {  1,  1, 0 }, {0,0,1}, {1,0} },
            { {  1, -1, 0 }, {0,0,1}, {1,1} },
        };
        vector<unsigned int> cpuIdx = { 0,1,2, 0,2,3 };
        modelA->mMeshes.push_back(Mesh(cpuVerts, cpuIdx, {}));
    }
    else
	    wcout << "Model loaded in " << loadElapsedSeconds.count() << " seconds." << endl;

    mModels.push_back(std::move(modelA));

	//loadStartTime = std::chrono::steady_clock::now();
	//const std::string modelPathB = GetResourcePath("Objects\\pkg_a_curtains\\NewSponza_Curtains_glTF.gltf").string();
	//auto modelB = std::make_unique<Model>();
	//modelB->loadModel(modelPathB);
	//loadEndTime = std::chrono::steady_clock::now();
	//loadElapsedSeconds = loadEndTime - loadStartTime;
	//wcout << "Model loaded in " << loadElapsedSeconds.count() << " seconds." << endl;
	//mModels.push_back(std::move(modelB));

	//loadStartTime = std::chrono::steady_clock::now();
	//const std::string modelPathC = GetResourcePath("Objects\\pkg_b_ivy\\NewSponza_IvyGrowth_glTF.gltf").string();
	//auto modelC = std::make_unique<Model>();
	//modelC->loadModel(modelPathC);
	//loadEndTime = std::chrono::steady_clock::now();
	//loadElapsedSeconds = loadEndTime - loadStartTime;
	//wcout << "Model loaded in " << loadElapsedSeconds.count() << " seconds." << endl;
	//mModels.push_back(std::move(modelC));



	std::chrono::steady_clock::time_point meshBuildStartTime = std::chrono::steady_clock::now();
    // Build GPU buffers and material descriptor tables
    BuildMeshGpuData();
	std::chrono::steady_clock::time_point meshBuildEndTime = std::chrono::steady_clock::now();
	std::chrono::duration<double> elapsedSeconds = meshBuildEndTime - meshBuildStartTime;
	wcout << "Mesh GPU data built in " << elapsedSeconds.count() << " seconds." << endl;
	// Initialize ray tracing acceleration structures
	std::chrono::steady_clock::time_point rtBuildStartTime = std::chrono::steady_clock::now();
	InitializeRayTracing();
	std::chrono::steady_clock::time_point rtBuildEndTime = std::chrono::steady_clock::now();
	std::chrono::duration<double> rtElapsedSeconds = rtBuildEndTime - rtBuildStartTime;
	wcout << "Ray tracing structures built in " << rtElapsedSeconds.count() << " seconds." << endl;

    // Collect static lights once after models are loaded
    CollectStaticLights();

	// For debugging: recompile shaders on 'G' key press
	InputManager::Instance.RegisterKeyPressedCallback('G', std::bind(&Renderer::InitializePipelineState, this));
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
	auto srvEmissive = mSrvHeap.Allocate();
    auto srvDepth = mSrvHeap.Allocate();

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
    // 1. Create Output Texture (UAV)
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



void Renderer::InitializeRootSignatures()
{
	mMeshRootSignature.InitializeMeshRS(mDevice.Get());
	mComputeRootSignature.InitializeComputeRS(mDevice.Get());
}

void Renderer::InitializePipelineState()
{
    HLSLShader vertexShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/VertexShader.hlsl", L"vs_6_0");
    HLSLShader pixelShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/PixelShader.hlsl", L"ps_6_0");
	HLSLShader computeShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/LightPassCS.hlsl", L"cs_6_5");
	HLSLShader transparentPixelShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/TransparentPixelShader.hlsl", L"ps_6_0");

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

    mPipelineStateCompute.InitializeCompute(
        mDevice.Get(),
        mComputeRootSignature.Get(),
		std::move(computeShader));
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
	mConstantBufferData.InvVpMatrix = DirectX::XMMatrixInverse(nullptr, viewProj);
    mConstantBufferData.viewPos = cameraPos;
	mConstantBufferData.frameCount = mFrameCount++;



  //  // --- use cached static lights, avoid re-scanning models each frame ---
  //  const int staticCount = static_cast<int>(std::min<size_t>(mStaticLights.size(), cMaxLights));

  //  // light camera light
  //  const float cameraLightIntensity = 0.15f;
  //  if (staticCount < cMaxLights)
  //  {
  //      LightData camLight{};
  //      camLight.position = DirectX::XMFLOAT4(
  //          cameraPos.x + cameraForward.x * 1000.0f,
  //          cameraPos.y + cameraForward.y * 1000.0f,
  //          cameraPos.z + cameraForward.z * 1000.0f,
  //          1.0f);
  //      camLight.diffuseColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, cameraLightIntensity);
		//camLight.specularColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, cameraLightIntensity);
  //      camLight.dirType = DirectX::XMFLOAT4(cameraForward.x, cameraForward.y, cameraForward.z, 1.0f); // directional flag
  //      mConstantBufferData.lights[staticCount] = camLight;
  //      mConstantBufferData.numLights = staticCount + 1;
  //  }
  //  else
  //  {
  //      // static lights already fill the limit; do not append camera light
  //      mConstantBufferData.numLights = staticCount;
  //  }

    UINT currentBackBufferIndex = mSwapChain.GetCurrentBackBufferIndex();
    size_t alignedSize = (sizeof(ConstantBufferData) + 255) & ~255; // Align to 256 bytes
    size_t cbOffset = alignedSize * currentBackBufferIndex;

    void* pData;
    mConstantBuffer.Get()->Map(0, nullptr, &pData);
	uint8_t* pByteData = reinterpret_cast<uint8_t*>(pData);
    memcpy(pByteData + cbOffset, &mConstantBufferData, sizeof(ConstantBufferData));
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

    // =========================================================================================
    // STAGE 1: G-BUFFER PASS (Rasterization)
    // =========================================================================================
    {
        // A. Transition G-Buffer Resources to RENDER_TARGET
		D3D12_RESOURCE_BARRIER barriers[5]{};
        barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
            mGBufferAlbedo.Get(),
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
		barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
			mGBufferNormal.Get(),
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_RENDER_TARGET);
		barriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(
			mGBufferMaterial.Get(),
			D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_RENDER_TARGET);
        barriers[3] = CD3DX12_RESOURCE_BARRIER::Transition(
            mDepthBuffer.GetResource(),
            D3D12_RESOURCE_STATE_COMMON,
            D3D12_RESOURCE_STATE_DEPTH_WRITE);
		barriers[4] = CD3DX12_RESOURCE_BARRIER::Transition(
            mGBufferEmission.Get(),
            D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_RENDER_TARGET);

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
		D3D12_RESOURCE_BARRIER barriers[6]{};
        barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
            mGBufferAlbedo.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
			mGBufferNormal.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
		barriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(
			mGBufferMaterial.Get(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        barriers[3] = CD3DX12_RESOURCE_BARRIER::Transition(
            mDepthBuffer.GetResource(),
            D3D12_RESOURCE_STATE_DEPTH_WRITE,
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
        barriers[4] = CD3DX12_RESOURCE_BARRIER::Transition(
            mComputeOutputTexture.Get(),
            D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
        barriers[5] = CD3DX12_RESOURCE_BARRIER::Transition(
            mGBufferEmission.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

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
        mCommandList.Get()->SetComputeRootDescriptorTable(4, mSrvHeap.GetGpuHandle(mUavSlot_Output));

        // D. Dispatch
        // Threads (8, 8, 1). Dispatch (Width/8, Height/8, 1)
        mCommandList.Get()->Dispatch((mWidth + 7) / 8, (mHeight + 7) / 8, 1);
    }

    // =========================================================================================
    // STAGE 2.5: TRANSPARENT FORWARD PASS
    // =========================================================================================
    {
        D3D12_RESOURCE_BARRIER barriers[2];

        // 1. Transition Output Texture: UAV (from Compute) -> RENDER_TARGET
        barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
            mComputeOutputTexture.Get(),
            D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
            D3D12_RESOURCE_STATE_RENDER_TARGET);

        // 2. Transition Depth: SHADER_RESOURCE (from Compute) -> DEPTH_READ
        // We need to READ depth to occlude glass behind walls, but we don't need to WRITE (usually).
        barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
            mDepthBuffer.GetResource(),
            D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
            D3D12_RESOURCE_STATE_DEPTH_READ);

        mCommandList.Get()->ResourceBarrier(2, barriers);

        // Bind Targets
        // We write Color to ComputeOutput, and Read Depth from DepthBuffer
        D3D12_CPU_DESCRIPTOR_HANDLE rtv = mGBufferRtvHeap.GetCpuHandle(mRtvIndex_ComputeOutput);
        D3D12_CPU_DESCRIPTOR_HANDLE dsv = mDepthBuffer.GetDSVHandle();

        mCommandList.Get()->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
        mCommandList.Get()->RSSetViewports(1, &mViewport);
        mCommandList.Get()->RSSetScissorRects(1, &mScissorRect);

        // Draw Transparent Meshes
        mCommandList.Get()->SetGraphicsRootSignature(mMeshRootSignature.Get());
        mCommandList.Get()->SetPipelineState(mPipelineStateTransparent.Get());
        mCommandList.Get()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        mCommandList.Get()->SetGraphicsRootConstantBufferView(0, mConstantBuffer.Get()->GetGPUVirtualAddress());

        for (const auto& mesh : mTransparentMeshes)
            DrawMesh(mesh);
    }

    // =========================================================================================
    // STAGE 3: COPY TO BACKBUFFER
    // =========================================================================================
    {
        D3D12_RESOURCE_BARRIER barriers[4]{};
		// Transition Output Texture -> Copy Source
        barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
            mComputeOutputTexture.Get(),
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            D3D12_RESOURCE_STATE_COPY_SOURCE);
        // Transition Back Buffer -> Copy Dest
        barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
            mSwapChain.GetCurrentBackBuffer(),
            D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_COPY_DEST);
		// Transition Depth Buffer back to Common
		barriers[2] = CD3DX12_RESOURCE_BARRIER::Transition(
			mDepthBuffer.GetResource(),
            D3D12_RESOURCE_STATE_DEPTH_READ,
			D3D12_RESOURCE_STATE_COMMON);
		// Transition Emissive G-Buffer back to Common
		barriers[3] = CD3DX12_RESOURCE_BARRIER::Transition(
			mGBufferEmission.Get(),
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_COMMON);

        mCommandList.Get()->ResourceBarrier(_countof(barriers), barriers);

        // Copy
        mCommandList.Get()->CopyResource(
            mSwapChain.GetCurrentBackBuffer(),
			mComputeOutputTexture.Get());

        // Transition Back Buffer -> Present
        D3D12_RESOURCE_BARRIER presentBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
            mSwapChain.GetCurrentBackBuffer(),
            D3D12_RESOURCE_STATE_COPY_DEST,
            D3D12_RESOURCE_STATE_PRESENT);
		mCommandList.Get()->ResourceBarrier(1, &presentBarrier);

        // Cleanup G-Buffer Transitions (Back to Common)
        D3D12_RESOURCE_BARRIER cleanup[4]{};
        cleanup[0] = CD3DX12_RESOURCE_BARRIER::Transition(mGBufferAlbedo.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
        cleanup[1] = CD3DX12_RESOURCE_BARRIER::Transition(mGBufferNormal.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
        cleanup[2] = CD3DX12_RESOURCE_BARRIER::Transition(mGBufferMaterial.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
		cleanup[3] = CD3DX12_RESOURCE_BARRIER::Transition(mComputeOutputTexture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON);
        mCommandList.Get()->ResourceBarrier(_countof(cleanup), cleanup);
    }

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
