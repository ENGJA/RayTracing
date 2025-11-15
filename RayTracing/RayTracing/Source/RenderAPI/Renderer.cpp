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


using std::wcout, std::endl;

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

	// Shader-visible SRV heap for textures
	mSrvHeap.Initialize(mDevice.Get(), 1024);
	mTextureLoader.Initialize(mDevice.Get(), &mSrvHeap);

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

	// Load a model via CPU-side loader (provide a file here as needed)
	mModel = std::make_unique<Model>();
	const std::string modelPath = "C:/Users/adria/Source/Repos/GK1/OpenGLDemo/Resources/objects/sphere/sphere.obj";
	mModel->loadModel(modelPath);

	// Fallback: if loader didn't populate the mesh, create a simple quad
	if (mModel->mMeshes.empty())
	{
		std::vector<::Vertex> cpuVerts = {
			{ { -1, -1, 0 }, {0,0,1}, {0,1} },
			{ { -1,  1, 0 }, {0,0,1}, {0,0} },
			{ {  1,  1, 0 }, {0,0,1}, {1,0} },
			{ {  1, -1, 0 }, {0,0,1}, {1,1} },
		};
		std::vector<unsigned int> cpuIdx = { 0,1,2, 0,2,3 };
		mModel->mMeshes.push_back(Mesh(cpuVerts, cpuIdx, {}));
	}

	// Create GPU VB/IB for first mesh in DEFAULT heap with UPLOAD staging
	const Mesh& mesh = mModel->mMeshes[0];
	const UINT vbSize = (UINT)(mesh.mVertices.size() * sizeof(::Vertex));
	const UINT ibSize = (UINT)(mesh.mIndices.size() * sizeof(unsigned int));

	// Default heap resources
	mVB.Initialize(mDevice.Get(), vbSize, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);
	mIB.Initialize(mDevice.Get(), ibSize, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COPY_DEST);

	// Upload buffers
	D3D12Resource vbUpload, ibUpload;
	vbUpload.Initialize(mDevice.Get(), vbSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
	ibUpload.Initialize(mDevice.Get(), ibSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);

	// Map and copy CPU data to upload heaps
	void* pData = nullptr;
	hr = vbUpload.Get()->Map(0, nullptr, &pData);
	ASSERT_HR(hr, "Failed to map VB upload buffer");
	memcpy(pData, mesh.mVertices.data(), vbSize);
	vbUpload.Get()->Unmap(0, nullptr);

	hr = ibUpload.Get()->Map(0, nullptr, &pData);
	ASSERT_HR(hr, "Failed to map IB upload buffer");
	memcpy(pData, mesh.mIndices.data(), ibSize);
	ibUpload.Get()->Unmap(0, nullptr);

	// Record copy and transitions on a transient command list
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> alloc;
	hr = mDevice.Get()->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(alloc.ReleaseAndGetAddressOf()));
	ASSERT_HR(hr, "Failed to create upload command allocator");
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> cmd;
	hr = mDevice.Get()->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc.Get(), nullptr, IID_PPV_ARGS(cmd.ReleaseAndGetAddressOf()));
	ASSERT_HR(hr, "Failed to create upload command list");

	cmd->CopyBufferRegion(mVB.Get(), 0, vbUpload.Get(), 0, vbSize);
	cmd->CopyBufferRegion(mIB.Get(), 0, ibUpload.Get(), 0, ibSize);

	D3D12_RESOURCE_BARRIER barriers[2]{};
	barriers[0].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[0].Transition.pResource = mVB.Get();
	barriers[0].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barriers[0].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER;

	barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[1].Transition.pResource = mIB.Get();
	barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_INDEX_BUFFER;

	cmd->ResourceBarrier(_countof(barriers), barriers);
	cmd->Close();

	ID3D12CommandList* lists[] = { cmd.Get() };
	mCommandQueue.Get()->ExecuteCommandLists(1, lists);
	mCommandQueue.Flush();

	mVBV.BufferLocation = mVB.Get()->GetGPUVirtualAddress();
	mVBV.SizeInBytes = vbSize;
	mVBV.StrideInBytes = sizeof(::Vertex);

	mIBV.BufferLocation = mIB.Get()->GetGPUVirtualAddress();
	mIBV.SizeInBytes = ibSize;
	mIBV.Format = DXGI_FORMAT_R32_UINT;

	// Load texture and create SRV
	mAlbedo = mTextureLoader.LoadTexture2DFromFile(L"C:/Users/adria/Source/Repos/GK1/OpenGLDemo/Resources/objects/sphere/diffuse.jpg");
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

	// Bind descriptor heaps
	ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
	mCommandList.Get()->SetDescriptorHeaps(_countof(heaps), heaps);

	// Initialize and set barrier
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

	// Draw
	mCommandList.Get()->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);
	mCommandList.Get()->RSSetViewports(1, &mViewport);
	mCommandList.Get()->RSSetScissorRects(1, &mScissorRect);

	mCommandList.Get()->SetGraphicsRootSignature(mPipelineState.GetRootSignature());
	mCommandList.Get()->SetPipelineState(mPipelineState.Get());
	mCommandList.Get()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mCommandList.Get()->IASetVertexBuffers(0, 1, &mVBV);
	mCommandList.Get()->IASetIndexBuffer(&mIBV);

	mCommandList.Get()->SetGraphicsRootConstantBufferView(0, mConstantBuffer.Get()->GetGPUVirtualAddress());
	mCommandList.Get()->SetGraphicsRootDescriptorTable(1, mAlbedo.srv.gpuHandle);

	mCommandList.Get()->DrawIndexedInstanced(mIBV.SizeInBytes / sizeof(UINT), 1, 0, 0, 0);

	// Change barrier states and set again
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

