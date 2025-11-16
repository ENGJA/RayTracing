#include "pch.h"

#include "D3D12/D3D12Debug.h"
#include "DataTypes.h"
#include "DXGI/DXGIDebug.h"
#include "DXGI/DXGIFactory.h"
#include "helpers.h"
#include "HLSL/HLSLCompiler.h"
#include "HLSL/HLSLShader.h"
#include "ResourceManager/Model.h"
#include "ResourceManager/TextureLoader.h"
#include "RenderAPI/Descriptors/ShaderVisibleDescriptorHeap.h"
#include "Renderer.h"

using std::wcout, std::endl, std::string, std::wstring, std::vector;

// Map texture type string to descriptor slot index
static int TextureTypeToSlot(const string& type)
{
    if (type == "texture_albedo") return 0; // BASE_COLOR
    if (type == "texture_normal") return 1;
    if (type == "texture_metalness") return 2;
    if (type == "texture_roughness") return 3;
    if (type == "texture_emissive") return 4;
    return -1;
}

GPUTexture Renderer::LoadOrGetTexture(const string& path)
{
    auto it = mTextureCache.find(path);
    if (it != mTextureCache.end()) return it->second;
	wstring wpath(path.begin(), path.end());
    GPUTexture tex = mTextureLoader.LoadTexture2DFromFile(wpath, mSwapChain.GetCurrentBackBufferIndex());
    mTextureCache.emplace(path, tex);
    return tex;
}

void Renderer::BuildMeshGpuData()
{
    for (const auto& modelPtr : mModels)
    {
        const Model& model = *modelPtr;
        for (const Mesh& mesh : model.mMeshes)
        {
            MeshGpuData gpu{};
            const UINT vbSize = (UINT)(mesh.mVertices.size() * sizeof(::Vertex));
            const UINT ibSize = (UINT)(mesh.mIndices.size() * sizeof(unsigned int));
            gpu.vb.Initialize(mDevice.Get(), vbSize, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
            gpu.ib.Initialize(mDevice.Get(), ibSize, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);

			mUploadHeap.Reset();
            auto vbAlloc = mUploadHeap.Allocate(vbSize);
            auto ibAlloc = mUploadHeap.Allocate(ibSize);
            if (!vbAlloc.cpuPtr || !ibAlloc.cpuPtr) throw std::runtime_error("Upload heap out of space for mesh buffers.");
            memcpy(vbAlloc.cpuPtr, mesh.mVertices.data(), vbSize);
            memcpy(ibAlloc.cpuPtr, mesh.mIndices.data(), ibSize);

            mCommandList.ResetCommandList(mSwapChain.GetCurrentBackBufferIndex());
            mCommandList.Get()->CopyBufferRegion(gpu.vb.Get(), 0, mUploadHeap.GetResource(), vbAlloc.offset, vbSize);
            mCommandList.Get()->CopyBufferRegion(gpu.ib.Get(), 0, mUploadHeap.GetResource(), ibAlloc.offset, ibSize);

            D3D12_RESOURCE_BARRIER barriers[2]{};
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
            mCommandList.Get()->Close();
            ID3D12CommandList* lists[] = { mCommandList.Get() };
            mCommandQueue.ExecuteCommandLists(1, lists);
            mCommandQueue.Flush();

            gpu.vbv.BufferLocation = gpu.vb.Get()->GetGPUVirtualAddress();
            gpu.vbv.SizeInBytes = vbSize;
            gpu.vbv.StrideInBytes = sizeof(::Vertex);
            gpu.ibv.BufferLocation = gpu.ib.Get()->GetGPUVirtualAddress();
            gpu.ibv.SizeInBytes = ibSize;
            gpu.ibv.Format = DXGI_FORMAT_R32_UINT;

            gpu.materialTable = mSrvHeap.Allocate(Config::cNumberOfTextureSlots);
            for (const Texture& cpuTex : mesh.mTextures)
            {
                int slot = TextureTypeToSlot(cpuTex.mType);
                if (slot < 0) continue;
                GPUTexture gpuTex = LoadOrGetTexture(model.mDirectory + "/" + cpuTex.mPath);
                D3D12_CPU_DESCRIPTOR_HANDLE dst = gpu.materialTable.cpuHandle;
                dst.ptr += SIZE_T(slot) * mDevice.Get()->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
                mDevice.Get()->CopyDescriptorsSimple(1, dst, gpuTex.srv.cpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
            }
            mMeshGpu.push_back(std::move(gpu));
        }
    }
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

    // Shader-visible SRV heap for textures (increase capacity for many material descriptors)
    mSrvHeap.Initialize(mDevice.Get(), 4096);

    // Shared upload heap (64 MB)
    mUploadHeap.Initialize(mDevice.Get(), 64ull * 1024ull * 1024ull);

    mTextureLoader.Initialize(mDevice.Get(), &mSrvHeap, &mCommandQueue, &mCommandList, &mUploadHeap);

    HLSLCompiler compiler;
    compiler.Initialize();

    HLSLShader vertexShader = compiler.CompileFromFile(L"Source/Shaders/VertexShader.hlsl", L"vs_6_0");
    HLSLShader pixelShader  = compiler.CompileFromFile(L"Source/Shaders/PixelShader.hlsl",  L"ps_6_0");

    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
    {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
        { "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
    };
    D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
    inputLayoutDesc.pInputElementDescs = inputElementDescs;
    inputLayoutDesc.NumElements = _countof(inputElementDescs);

    mPipelineState.Initialize(
        mDevice.Get(),
        std::move(vertexShader),
        std::move(pixelShader),
        inputLayoutDesc);

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

    // View-projection matrix
    DirectX::XMMATRIX viewMatrix = DirectX::XMMatrixLookAtLH(
        { 0.0f, 1.0f, -3.0f, 0.0f },
        { 0.0f, 0.0f, 0.0f, 0.0f },
        { 0.0f, 1.0f, 0.0f, 0.0f });
    DirectX::XMMATRIX translation = DirectX::XMMatrixTranslation(0.0f, -1.0f, 1.0f);
    viewMatrix = translation * viewMatrix;
    DirectX::XMMATRIX projectionMatrix = DirectX::XMMatrixPerspectiveFovLH(1.2217304764f, 16.0f / 9.0f, 1.0f, 50.0f);
    mConstantBufferData.vpMatrix = viewMatrix * projectionMatrix;

    mConstantBuffer.Initialize(
        mDevice.Get(),
        sizeof(DirectX::XMMATRIX),
        D3D12_HEAP_TYPE_UPLOAD,
        D3D12_RESOURCE_STATE_GENERIC_READ);

    // Load multiple models (example: load same sphere twice to exercise texture sharing)
    const std::string modelPath = "C:/Users/adria/Source/Repos/GK1/OpenGLDemo/Resources/objects/sphere/sphere.obj";
    auto modelA = std::make_unique<Model>();
    modelA->loadModel(modelPath);
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
    mModels.push_back(std::move(modelA));

    auto modelB = std::make_unique<Model>();
    modelB->loadModel(modelPath); // same path to test cache
    if (modelB->mMeshes.empty())
    {
        vector<::Vertex> cpuVerts = {
            { { -0.5f, -0.5f, 0 }, {0,0,1}, {0,1} },
            { { -0.5f,  0.5f, 0 }, {0,0,1}, {0,0} },
            { {  0.5f,  0.5f, 0 }, {0,0,1}, {1,0} },
            { {  0.5f, -0.5f, 0 }, {0,0,1}, {1,1} },
        };
        vector<unsigned int> cpuIdx = { 0,1,2, 0,2,3 };
        modelB->mMeshes.push_back(Mesh(cpuVerts, cpuIdx, {}));
    }
    mModels.push_back(std::move(modelB));

    // Build GPU buffers and material descriptor tables
    BuildMeshGpuData();
}

void Renderer::Update()
{
    static float angle = 0.0f;
    angle += 0.01f;
    DirectX::XMMATRIX rotationMatrix = DirectX::XMMatrixRotationY(angle);
    DirectX::XMMATRIX worldViewProj = rotationMatrix * mConstantBufferData.vpMatrix;
    void* pData;
    mConstantBuffer.Get()->Map(0, nullptr, &pData);
    memcpy(pData, &worldViewProj, sizeof(DirectX::XMMATRIX));
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

