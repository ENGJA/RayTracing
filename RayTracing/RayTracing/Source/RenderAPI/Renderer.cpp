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

	mRtBuilder.Initialize(mDevice.Get(), mCommandList.Get(), &mCommandQueue);


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

    
    const std::string modelPath = GetResourcePath("Objects\\sponza\\NewSponza_Main_glTF_003.gltf").string(); 
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
    InputManager::Instance.RegisterKeyPressedCallback('R', [this]() { mRayTracingEnabled = !mRayTracingEnabled; });
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
    mRtBuilder.BuildAllBLAS(
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
    constexpr UINT uploadAlignment = 256;
	UINT64 alignedSize = (instanceDescSize + uploadAlignment - 1) & ~(uploadAlignment - 1);
    mInstanceDescBuffer.Initialize(
        mDevice.Get(),
        instanceDescSize,
        D3D12_HEAP_TYPE_UPLOAD,
		D3D12_RESOURCE_STATE_GENERIC_READ);

    // 3. Build TLAS
    mRtBuilder.BuildTLAS(
        mOpaqueSingleSidedMeshes,
        mOpaqueDoubleSidedMeshes,
		mMaskedSingleMeshes,
		mMaskedDoubleSidedMeshes,
        mTransparentMeshes,
        mTLAS,
		mTLAS_Scratch,
        mInstanceDescBuffer);

    mRtConstantBuffer.Initialize(
        mDevice.Get(),
        sizeof(RayGenConstantBuffer),
        D3D12_HEAP_TYPE_UPLOAD,
		D3D12_RESOURCE_STATE_GENERIC_READ);

    CreateRayTracingOutput();
    CreateRayTracingPipeline();
    CreateShaderBindingTable();

	// 4. Execute command list
    mCommandList.Get()->Close();
    ID3D12CommandList* lists[] = { mCommandList.Get() };
    mCommandQueue.ExecuteCommandLists(1, lists);
	mCommandQueue.Flush();

	// 5. Clear temporary BLAS resources
	mRtBuilder.ClearScratchResources();
}

void Renderer::CreateRayTracingOutput()
{
    D3D12_RESOURCE_DESC desc = {};
    desc.DepthOrArraySize = 1;
    desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    desc.Width = mWidth;
    desc.Height = mHeight;
    desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    desc.MipLevels = 1;
    desc.SampleDesc = { 1, 0 };

    mRtOutputResource.Initialize(mDevice.Get(), desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    // Create UAV Descriptor (We need a free slot in a Visible Heap)
    // For simplicity, let's assume we allocate one from mSrvHeap or a new one.
    // Ideally, mSrvHeap handles this. Let's assume Allocate() works.
    auto uavHandle = mSrvHeap.Allocate(1);
    mRtOutputUavCpuHandle = uavHandle.cpuHandle;
	mRtOutputUavGpuHandle = uavHandle.gpuHandle;

    D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
    uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    mDevice.Get()->CreateUnorderedAccessView(mRtOutputResource.Get(), nullptr, &uavDesc, mRtOutputUavCpuHandle);
}

void Renderer::CreateRayTracingPipeline()
{
    // 1. Create Global Root Signature
    // Needs: Output UAV (u0), TLAS (t0), Camera CB (b0)
    CD3DX12_DESCRIPTOR_RANGE uavRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // u0
    //CD3DX12_DESCRIPTOR_RANGE srvRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0); // t0

    CD3DX12_ROOT_PARAMETER params[3];
    params[0].InitAsDescriptorTable(1, &uavRange); // u0
	params[1].InitAsShaderResourceView(0);      // t0
    params[2].InitAsConstantBufferView(0);         // b0

    CD3DX12_ROOT_SIGNATURE_DESC globalRootDesc(3, params);

    // Serialize & Create
    Microsoft::WRL::ComPtr<ID3DBlob> blob;
    Microsoft::WRL::ComPtr<ID3DBlob> error;
    D3D12SerializeRootSignature(&globalRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
    mDevice.Get()->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(&mRtGlobalRootSignature));

    // 2. Load Compiled Shader (RayTracing.cso)
    // Ensure you configured VS to compile RayTracing.hlsl to .cso!
    HLSLShader rtShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/RayTracing.hlsl", L"lib_6_3", {}, L"");
	auto shaderBlob = rtShader.GetShaderBlob();

    // 3. Create State Object (The Complex Part)
    CD3DX12_STATE_OBJECT_DESC rtPipe(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

    // Library (The Shader Blob)
    auto lib = rtPipe.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
    D3D12_SHADER_BYTECODE libdxil = { shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize() };
    lib->SetDXILLibrary(&libdxil);
    // Export symbols (Entry points)
    lib->DefineExport(L"MyRayGen");
    lib->DefineExport(L"MyMiss");
    lib->DefineExport(L"MyClosestHit");

    // Hit Groups
    // We bind "MyClosestHit" to a hit group named "HitGroup0"
    auto hitGroup = rtPipe.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
    hitGroup->SetClosestHitShaderImport(L"MyClosestHit");
    hitGroup->SetHitGroupExport(L"HitGroup0");
    hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

    // Shader Config (Payload Size)
    auto shaderConfig = rtPipe.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
    shaderConfig->Config(sizeof(float) * 4, sizeof(float) * 2); // 16 byte payload, 8 byte attributes

    // Global Root Signature
    auto globalRoot = rtPipe.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
    globalRoot->SetRootSignature(mRtGlobalRootSignature.Get());

    // Pipeline Config (Recursion Depth)
    auto pipelineConfig = rtPipe.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
    pipelineConfig->Config(1);

    // Create
    HRESULT hr = mDevice.Get()->CreateStateObject(rtPipe, IID_PPV_ARGS(mRtStateObject.ReleaseAndGetAddressOf()));
    if (FAILED(hr)) throw std::runtime_error("Failed to create RTPSO");
}

void Renderer::CreateShaderBindingTable()
{
    Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> props;
    mRtStateObject.As(&props);

    // Get Shader Identifiers
    void* rayGenID = props->GetShaderIdentifier(L"MyRayGen");
    void* missID = props->GetShaderIdentifier(L"MyMiss");
    void* hitGroupID = props->GetShaderIdentifier(L"HitGroup0");

    // Calculate Size (Aligned to 256 bytes usually, records aligned to 32)
    UINT shaderIDSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES; // 32
    UINT recordSize = 32; // Standard alignment for shader records

    mSbtEntrySize= recordSize;
    UINT sbtSize = recordSize * 3; // RayGen + Miss + HitGroup

    // Allocate Upload Buffer for SBT    
    mSbtResource.Initialize(mDevice.Get(), sbtSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);

    // Write Data
    uint8_t* pData;
    mSbtResource.Get()->Map(0, nullptr, (void**)&pData);

    // Entry 0: RayGen
    memcpy(pData, rayGenID, shaderIDSize);

    // Entry 1: Miss
    memcpy(pData + recordSize, missID, shaderIDSize);

    // Entry 2: HitGroup (For Opaque)
    memcpy(pData + recordSize * 2, hitGroupID, shaderIDSize);

    mSbtResource.Get()->Unmap(0, nullptr);
}

void Renderer::RenderRayTracing(const DirectX::XMMATRIX& viewProj, const DirectX::XMFLOAT3& camPos)
{
    auto cmdList = static_cast<ID3D12GraphicsCommandList4*>(mCommandList.Get());

    // 1. Bind Pipeline & Resources
    ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
    cmdList->SetDescriptorHeaps(1, heaps);

    cmdList->SetPipelineState1(mRtStateObject.Get());
    cmdList->SetComputeRootSignature(mRtGlobalRootSignature.Get());

    // Slot 0: UAV Table (Points to mRtOutputUAV_Gpu)
    // Note: D3D12 Requires a table for UAVs in Root Sigs, or we use SetComputeRootUnorderedAccessView if it's a Raw buffer.
    // For Texture2D UAV, Table is standard.
    cmdList->SetComputeRootDescriptorTable(0, mRtOutputUavGpuHandle);

    // Slot 1: TLAS (SRV)
    cmdList->SetComputeRootShaderResourceView(1, mTLAS.Get()->GetGPUVirtualAddress());

    // Slot 2: Camera CB
    RayGenConstantBuffer cb;
    cb.viewProjInverse = DirectX::XMMatrixInverse(nullptr, viewProj);
    cb.cameraPos = { camPos.x, camPos.y, camPos.z, 1.0f };

  
	void* pData;
	mRtConstantBuffer.Get()->Map(0, nullptr, &pData);
	memcpy(pData, &cb, sizeof(RayGenConstantBuffer));
	mRtConstantBuffer.Get()->Unmap(0, nullptr);
	cmdList->SetComputeRootConstantBufferView(2, mRtConstantBuffer.Get()->GetGPUVirtualAddress());


    // 2. Dispatch
    D3D12_DISPATCH_RAYS_DESC desc = {};
    desc.RayGenerationShaderRecord.StartAddress = mSbtResource.Get()->GetGPUVirtualAddress();
    desc.RayGenerationShaderRecord.SizeInBytes = mSbtEntrySize;

    desc.MissShaderTable.StartAddress = mSbtResource.Get()->GetGPUVirtualAddress() + mSbtEntrySize;
    desc.MissShaderTable.SizeInBytes = mSbtEntrySize;
    desc.MissShaderTable.StrideInBytes = mSbtEntrySize;

    desc.HitGroupTable.StartAddress = mSbtResource.Get()->GetGPUVirtualAddress() + mSbtEntrySize * 2;
    desc.HitGroupTable.SizeInBytes = mSbtEntrySize * 2; // HG0 + HG1
    desc.HitGroupTable.StrideInBytes = mSbtEntrySize;

    desc.Width = mWidth;
    desc.Height = mHeight;
    desc.Depth = 1;

    cmdList->DispatchRays(&desc);

    // 3. Copy UAV -> BackBuffer
    // Transition BackBuffer to COPY_DEST
    D3D12_RESOURCE_BARRIER b1 = CD3DX12_RESOURCE_BARRIER::Transition(
        mSwapChain.GetCurrentBackBuffer(),
        D3D12_RESOURCE_STATE_PRESENT,
        D3D12_RESOURCE_STATE_COPY_DEST);

    // Transition Output UAV to COPY_SOURCE
    D3D12_RESOURCE_BARRIER b2 = CD3DX12_RESOURCE_BARRIER::Transition(
        mRtOutputResource.Get(),
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);

    D3D12_RESOURCE_BARRIER barriers[] = { b1, b2 };
    cmdList->ResourceBarrier(2, barriers);

    cmdList->CopyResource(mSwapChain.GetCurrentBackBuffer(), mRtOutputResource.Get());

    // Restore States
    D3D12_RESOURCE_BARRIER b3 = CD3DX12_RESOURCE_BARRIER::Transition(
        mSwapChain.GetCurrentBackBuffer(),
        D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_PRESENT);

    D3D12_RESOURCE_BARRIER b4 = CD3DX12_RESOURCE_BARRIER::Transition(
        mRtOutputResource.Get(),
        D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

    D3D12_RESOURCE_BARRIER restoreBarriers[] = { b3, b4 };
    cmdList->ResourceBarrier(2, restoreBarriers);
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
        mConstantBufferData.numLights = staticCount + 1;
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


    // Wait for GPU to finish with the current back buffer
    mCommandQueue.WaitForFenceInFrame(mSwapChain.GetCurrentBackBufferIndex());

    // Open command list
    mCommandList.ResetCommandList(mSwapChain.GetCurrentBackBufferIndex());
    if (mRayTracingEnabled)
    {
        // Ray tracing rendering path
        RenderRayTracing(viewProj, cameraPos);
    }
	else
    {
        // Sort transparent meshes back-to-front each frame (temporary solution)
        SortTransparentMeshes(cameraPos);

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

        const FLOAT clearColor[4] = { 0 };

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

        // Transition back buffer to present
        barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        mCommandList.Get()->ResourceBarrier(1, &barrier);

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
