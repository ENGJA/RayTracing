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

    for (int i = 0; i < Config::cNumberOfTextureSlots; i++)
    {
        D3D12_CPU_DESCRIPTOR_HANDLE dst = gpuData.materialTable.cpuHandle;
        dst.ptr += SIZE_T(i) * mDevice.Get()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

        auto it = textureMap.find(i);
        if (it != textureMap.end())
        {
            string fullPath = directory + "\\" + it->second;
            GpuTextureLoadState& loadState = mTextureCache[fullPath];
            if (loadState.decodeFuture.valid())
            {
                loadState.decodeFuture.wait();
                loadState.decodedImage = loadState.decodeFuture.get();
            }

            if (!loadState.gpuTexture.resource.Get())
                loadState.gpuTexture = mTextureLoader.CreateTextureFromDecodedImage(loadState.decodedImage, executeBatch);

            GPUTexture& gpuTex = loadState.gpuTexture;
            CreateTextureView(gpuTex.resource.Get(), gpuTex.format, dst, gpuTex.mipLevels);
        }
        else
        {
            CreateTextureView(nullptr, DXGI_FORMAT_R8G8B8A8_UNORM, dst, 1);
        }
    }
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

    mMeshGpu.push_back(std::move(gpu));
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
    mUploadHeap.Initialize(mDevice.Get(), 64ull * 1024ull * 1024ull);

	mShaderCompiler.Initialize();

    InitializeTextureLoader();

    InitializePipelineState();

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

	std::chrono::steady_clock::time_point meshBuildStartTime = std::chrono::steady_clock::now();
    // Build GPU buffers and material descriptor tables
    BuildMeshGpuData();
	std::chrono::steady_clock::time_point meshBuildEndTime = std::chrono::steady_clock::now();
	std::chrono::duration<double> elapsedSeconds = meshBuildEndTime - meshBuildStartTime;
	wcout << "Mesh GPU data built in " << elapsedSeconds.count() << " seconds." << endl;

    // Collect static lights once after models are loaded
    CollectStaticLights();

	// For debugging: recompile shaders on 'G' key press
	InputManager::Instance.RegisterKeyPressedCallback('G', std::bind(&Renderer::InitializePipelineState, this));
}

void Renderer::InitializePipelineState()
{
    HLSLShader vertexShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/VertexShader.hlsl", L"vs_6_0");
    HLSLShader pixelShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/PixelShader.hlsl", L"ps_6_0");

    constexpr D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc
    {
        .pInputElementDescs = inputElementDescs,
        .NumElements = _countof(inputElementDescs),
    };

	mCommandQueue.Flush();
    mPipelineState.Initialize(
        mDevice.Get(),
        std::move(vertexShader),
        std::move(pixelShader),
        inputLayoutDesc);
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
        camLight.color = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, cameraLightIntensity);
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

    mCommandList.Get()->SetGraphicsRootSignature(mPipelineState.GetRootSignature());
    mCommandList.Get()->SetPipelineState(mPipelineState.Get());
    mCommandList.Get()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    mCommandList.Get()->SetGraphicsRootConstantBufferView(0, mConstantBuffer.Get()->GetGPUVirtualAddress());

    // Draw all meshes
    for (const auto& mesh : mMeshGpu)
    {
        mCommandList.Get()->IASetVertexBuffers(0, 1, &mesh.vbv);
        mCommandList.Get()->IASetIndexBuffer(&mesh.ibv);
        mCommandList.Get()->SetGraphicsRootDescriptorTable(1, mesh.materialTable.gpuHandle);
        mCommandList.Get()->DrawIndexedInstanced(mesh.ibv.SizeInBytes / sizeof(UINT), 1, 0, 0, 0);
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
