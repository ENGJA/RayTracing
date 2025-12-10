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
#include "RenderAPI/Descriptors/DescriptorHeap.h"
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
	//gpu.vb.Initialize(mDevice.Get(), vbSizeUINT, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_GENERIC_READ);
	//gpu.ib.Initialize(mDevice.Get(), ibSizeUINT, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_GENERIC_READ);

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
	barriers[0].Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;

	barriers[1].Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
	barriers[1].Transition.pResource = gpu.ib.Get();
	barriers[1].Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
	barriers[1].Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
	barriers[1].Transition.StateAfter = D3D12_RESOURCE_STATE_GENERIC_READ;

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
			mMaskedSingleSidedMeshes.push_back(std::move(gpu));
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
	mSrvHeap.Initialize(mDevice.Get(), Config::cNumberOfSrvDescriptors, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
	mSrvHeap.Get()->SetName(L"Shader-visible SRV Heap");
	mCpuHeap.Initialize(mDevice.Get(), 128, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, false);
	mCpuHeap.Get()->SetName(L"CPU-only Descriptor Heap");
	mFrameHeap.Initialize(mDevice.Get(), 2048, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);
	mFrameHeap.Get()->SetName(L"Frame Descriptor Heap");

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



	//const std::string modelPath = R"(C:\Users\adria\Source\glTF-Sample-Assets\Models\AlphaBlendModeTest\glTF\AlphaBlendModeTest.gltf)"; GetResourcePath("Objects\\sponza\\NewSponza_Main_glTF_003.gltf").string();
	//const std::string modelPath = R"(C:\Users\adria\Source\glTF-Sample-Assets\Models\AlphaBlendModeTest\glTF\AlphaBlendModeTest.gltf)"; GetResourcePath("Objects\\sponza\\NewSponza_Main_glTF_003.gltf").string();
	//const std::string modelPath = R"(C:\Users\adria\Source\glTF-Sample-Assets\Models\ABeautifulGame\glTF\ABeautifulGame.gltf)"; GetResourcePath("Objects\\sponza\\NewSponza_Main_glTF_003.gltf").string();
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

	InitializeMotionVectors();
	InitializeDenoising();
	InitializeNRD();
	InitializeCompositePipeLineState();

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

	D3D12_RESOURCE_BARRIER barrier = CD3DX12_RESOURCE_BARRIER::Transition(
		mDepthBuffer.GetResource(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		D3D12_RESOURCE_STATE_COMMON
	);
	mCommandList.Get()->ResourceBarrier(1, &barrier);

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
		mMaskedSingleSidedMeshes,
		mMaskedDoubleSidedMeshes,
		mTransparentMeshes);

	// 2. Allocate TLAS instance desc buffer
	UINT totalMeshes = static_cast<UINT>(
		mOpaqueSingleSidedMeshes.size() +
		mOpaqueDoubleSidedMeshes.size() +
		mMaskedSingleSidedMeshes.size() +
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
		mMaskedSingleSidedMeshes,
		mMaskedDoubleSidedMeshes,
		mTransparentMeshes,
		mTLAS,
		mTLAS_Scratch,
		mInstanceDescBuffer);

	mRtConstantBufferStride = (sizeof(RayGenConstantBuffer) + 255) & ~255; // 256-byte aligned
	const UINT totalRtCBSize = mRtConstantBufferStride * Config::cBufferCount;

	mRtConstantBuffer.Initialize(
		mDevice.Get(),
		totalRtCBSize, // 256-byte aligned
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

void Renderer::InitializeDenoising()
{
	//   // 1. Create History Texture (Same format as RT Output)
	//   CD3DX12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
	//       DXGI_FORMAT_R16G16B16A16_FLOAT,
	//       mWidth,
	//       mHeight,
	//       1, // array size
	//       1  // mip levels
	   //);

	////   mHistoryTexture.Initialize(
	////       mDevice.Get(),
	////       desc,
	////       D3D12_HEAP_TYPE_DEFAULT,
	////       D3D12_RESOURCE_STATE_COMMON
	   ////);

	//   // 2. Create Denoise Output Texture (UAV)
	   //desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	//   mDenoiseOutput.Initialize(
	//       mDevice.Get(),
	//       desc,
	//       D3D12_HEAP_TYPE_DEFAULT,
	//       D3D12_RESOURCE_STATE_UNORDERED_ACCESS
	   //);

	//   // 3. Allocate Descriptors
	//   // We need 4 slots now: 
	//   // [0] t0: Noisy Input (SRV)
	//   // [1] t1: History Input (SRV)
	//   // [2] t2: Motion Vectors (SRV)
	//   // [3] u0: Output (UAV)
	//   mDenoiseDescriptorTable = mSrvHeap.Allocate(4);
	//   UINT inc = mSrvHeap.GetIncrementSize();

	//   // Create Views
	//   // Slot 0: Noisy Input (R16G16B16A16_FLOAT)
	   //CreateTextureView(mRtOutputResource.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, mDenoiseDescriptorTable.GetCpuHandle(0, inc), 1);

	//   // Slot 1: History Input
	   ////CreateTextureView(mHistoryTexture.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, mDenoiseDescriptorTable.GetCpuHandle(1, inc), 1);

	//   // Slot 2: Motion Vectors (R16G16_FLOAT)
	//   CreateTextureView(mMotionVectorTexture.Get(), DXGI_FORMAT_R16G16_FLOAT, mDenoiseDescriptorTable.GetCpuHandle(2, inc), 1);

	//   // Slot 3: Output UAV
	//   //auto uavAlloc = mSrvHeap.Allocate(1);
	//   //// Store the handle
	//   //mDenoiseOutputUavCpuHandle = uavAlloc.cpuHandle;

	//   D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	//   uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT; // Output format
	//   uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	//   uavDesc.Texture2D.MipSlice = 0;

	//   //mDevice.Get()->CreateUnorderedAccessView(mDenoiseOutput.Get(), nullptr, &uavDesc, mDenoiseOutputUavCpuHandle);

	   //auto stagingUavAlloc = mCpuHeap.Allocate(1);
	   //mDenoiseOutputUavCpuHandle = stagingUavAlloc.cpuHandle;

	   //mDevice.Get()->CreateUnorderedAccessView(mDenoiseOutput.Get(), nullptr, &uavDesc, mDenoiseOutputUavCpuHandle);

	//   //CreateDenoisePipeline();    
}

void Renderer::CreateDenoisePipeline()
{
	HLSLShader denoiseShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/DenoiseCS.hlsl", L"cs_6_0");
	mDenoisePipelineState.InitializeCompute(mDevice.Get(), std::move(denoiseShader));
}

void Renderer::InitializeMotionVectors()
{
	// 1. Create Motion Vector Texture (R16G16_FLOAT for precision)
	D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R16G16_FLOAT, // 2 Channels (X, Y Velocity)
		mWidth, mHeight,
		1, 1, 1, 0,
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
	);

	// Optimized Clear Value (0 velocity)
	D3D12_CLEAR_VALUE clearVal = {};
	clearVal.Format = DXGI_FORMAT_R16G16_FLOAT;
	clearVal.Color[0] = 0.0f;
	clearVal.Color[1] = 0.0f;

	mMotionVectorTexture.Initialize(mDevice.Get(), desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, &clearVal);

	// 2. Create RTV Descriptor Heap (Capacity 1 for MV)
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
	rtvHeapDesc.NumDescriptors = 1;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	mRtvHeap.Initialize(mDevice.Get(), rtvHeapDesc);

	mMotionVectorRtvHandle = mRtvHeap.Get()->GetCPUDescriptorHandleForHeapStart();
	mDevice.Get()->CreateRenderTargetView(mMotionVectorTexture.Get(), nullptr, mMotionVectorRtvHandle);

	// 3. Create PSO
	// Compile your new shaders
	HLSLShader vs = mShaderCompiler.CompileFromFile(L"Source/Shaders/MotionGenVertexShader.hlsl", L"vs_6_0");
	HLSLShader ps = mShaderCompiler.CompileFromFile(L"Source/Shaders/MotionGenPixelShader.hlsl", L"ps_6_0");

	// Re-use the existing Input Layout from InitializePipelineState
	constexpr D3D12_INPUT_ELEMENT_DESC inputElementDescs[] =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",   0, DXGI_FORMAT_R32G32B32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT",  0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 1, DXGI_FORMAT_R32G32_FLOAT,    0, D3D12_APPEND_ALIGNED_ELEMENT, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
	D3D12_INPUT_LAYOUT_DESC inputLayout = { inputElementDescs, _countof(inputElementDescs) };

	// Initialize with the FLOAT16 format
	mMotionVectorPipelineState.InitializeOpaque(
		mDevice.Get(),
		std::move(vs),
		std::move(ps),
		inputLayout,
		false,
		DXGI_FORMAT_R16G16_FLOAT // <--- Important!
	);


	auto srvAlloc = mCpuHeap.Allocate(1);
	mMotionVectorSrvCpuHandle = srvAlloc.cpuHandle;

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc{};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = DXGI_FORMAT_R16G16_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;

	mDevice.Get()->CreateShaderResourceView(mMotionVectorTexture.Get(), &srvDesc, mMotionVectorSrvCpuHandle);
}

void Renderer::RenderMotionVectors()
{
	// 1. Transition Motion Vector Texture
	D3D12_RESOURCE_BARRIER barriers[2]{};
	barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
		mMotionVectorTexture.Get(),
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, // Or SRV if coming from prev frame
		D3D12_RESOURCE_STATE_RENDER_TARGET
	);
	// 2. Transition Depth Buffer
	barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
		mDepthBuffer.GetResource(),
		D3D12_RESOURCE_STATE_COMMON,
		D3D12_RESOURCE_STATE_DEPTH_WRITE
	);
	mCommandList.Get()->ResourceBarrier(_countof(barriers), barriers);

	// Clear
	const float clearColor[] = { 0.0f, 0.0f, 0.0f, 0.0f };
	mCommandList.Get()->ClearRenderTargetView(mMotionVectorRtvHandle, clearColor, 0, nullptr);

	mCommandList.Get()->ClearDepthStencilView(
		mDepthBuffer.GetDSVHandle(),
		D3D12_CLEAR_FLAG_DEPTH,
		1.0f,
		0,
		0,
		nullptr
	);

	ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
	mCommandList.Get()->SetDescriptorHeaps(_countof(heaps), heaps);

	// Bind Targets
	// Note: We bind the Depth Buffer too so we can Z-Test!
	D3D12_CPU_DESCRIPTOR_HANDLE dsv = mDepthBuffer.GetDSVHandle();
	mCommandList.Get()->OMSetRenderTargets(1, &mMotionVectorRtvHandle, FALSE, &dsv);

	// Set State
	mCommandList.Get()->SetPipelineState(mMotionVectorPipelineState.Get());
	mCommandList.Get()->SetGraphicsRootSignature(mMotionVectorPipelineState.GetRootSignature()); // Reuses generic Mesh Root Sig
	mCommandList.Get()->RSSetViewports(1, &mViewport);
	mCommandList.Get()->RSSetScissorRects(1, &mScissorRect);

	mCommandList.Get()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// Bind Constants (ViewProj + PrevViewProj)
	mCommandList.Get()->SetGraphicsRootConstantBufferView(0, mConstantBuffer.Get()->GetGPUVirtualAddress());

	// Draw Opaque Meshes
	// Note: Motion Vectors for transparent objects are complex. Usually skipped or handled separately.
	for (const auto& mesh : mOpaqueSingleSidedMeshes) DrawMesh(mesh);
	for (const auto& mesh : mOpaqueDoubleSidedMeshes) DrawMesh(mesh);
	for (const auto& mesh : mMaskedSingleSidedMeshes) DrawMesh(mesh);
	for (const auto& mesh : mMaskedDoubleSidedMeshes) DrawMesh(mesh);

	// 3. Cleanup Transitions
	D3D12_RESOURCE_BARRIER cleanupBarriers[2];

	// MV Texture: RT -> SRV (Ready for Denoising Shader to read)
	cleanupBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
		mMotionVectorTexture.Get(),
		D3D12_RESOURCE_STATE_RENDER_TARGET,
		D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
	);

	// Depth Buffer: WRITE -> COMMON (Clean state for next frame/pass)
	cleanupBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
		mDepthBuffer.GetResource(),
		D3D12_RESOURCE_STATE_DEPTH_WRITE,
		D3D12_RESOURCE_STATE_COMMON
	);
	mCommandList.Get()->ResourceBarrier(_countof(cleanupBarriers), cleanupBarriers);
}

void Renderer::CreateNrdRootSignature(const nrd::PipelineDesc& pipeDesc, uint32_t index)
{
	std::vector<CD3DX12_ROOT_PARAMETER> params;
	std::vector<CD3DX12_DESCRIPTOR_RANGE> ranges;

	// Reserve memory to prevent pointer invalidation when pushing back
	ranges.resize(pipeDesc.resourceRangesNum);

	// --- Counters to track register indices ---
	UINT currentBaseSRV = 0; // Tracks t0, t1, t2...
	UINT currentBaseUAV = 0; // Tracks u0, u1, u2...

	for (uint32_t r = 0; r < pipeDesc.resourceRangesNum; ++r)
	{
		const nrd::ResourceRangeDesc& nrdRange = pipeDesc.resourceRanges[r];

		D3D12_DESCRIPTOR_RANGE_TYPE rangeType;
		UINT baseRegister = 0;
		if (nrdRange.descriptorType == nrd::DescriptorType::TEXTURE)
		{
			rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
			baseRegister = currentBaseSRV;
			currentBaseSRV += nrdRange.descriptorsNum; // Increment for next range
		}
		else if (nrdRange.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE)
		{
			rangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
			baseRegister = currentBaseUAV;
			currentBaseUAV += nrdRange.descriptorsNum; // Increment for next range
		}
		else
		{
			// Should not happen in current NRD, but safety break
			continue;
		}

		// Map NRD range to D3D12 range
		// baseRegisterIndex is the shader register (e.g., t0, u0)
		ranges[r].Init(rangeType, nrdRange.descriptorsNum, baseRegister);

		CD3DX12_ROOT_PARAMETER param;
		param.InitAsDescriptorTable(1, &ranges[r]);
		params.push_back(param);
	}

	// Add Root Constant / CBV
	// NRD usually requires a Constant Buffer at b0
	CD3DX12_DESCRIPTOR_RANGE cbvRange{};
	if (pipeDesc.hasConstantData)
	{
		// Define it as a TABLE RANGE, not a Root View
		// Type: CBV, Count: 1, Register: b0, Space: 1
		cbvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0, 1);

		CD3DX12_ROOT_PARAMETER cbParam{};
		cbParam.InitAsDescriptorTable(1, &cbvRange);
		params.push_back(cbParam);
	}
	// Static Samplers (Required by NRD)
	CD3DX12_STATIC_SAMPLER_DESC staticSamplers[2]{};
	staticSamplers[0].Init(
		0,                                  // ShaderRegister (s0)
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,    // Filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,   // AddressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,   // AddressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,   // AddressW
		0.0f,                               // MipLODBias
		16,                                 // MaxAnisotropy
		D3D12_COMPARISON_FUNC_NEVER,        // ComparisonFunc
		D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK, // BorderColor
		0.0f,                               // MinLOD
		D3D12_FLOAT32_MAX,                  // MaxLOD
		D3D12_SHADER_VISIBILITY_ALL,        // Visibility
		1                                   // RegisterSpace (KEY FIX: Must be 1)
	);

	// Sampler 1: Point (s1, space1)
	staticSamplers[1].Init(
		1,                                  // ShaderRegister (s1)
		D3D12_FILTER_MIN_MAG_MIP_POINT,     // Filter
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,   // AddressU
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,   // AddressV
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,   // AddressW
		0.0f,                               // MipLODBias
		16,                                 // MaxAnisotropy
		D3D12_COMPARISON_FUNC_NEVER,        // ComparisonFunc
		D3D12_STATIC_BORDER_COLOR_TRANSPARENT_BLACK, // BorderColor
		0.0f,                               // MinLOD
		D3D12_FLOAT32_MAX,                  // MaxLOD
		D3D12_SHADER_VISIBILITY_ALL,        // Visibility
		1                                   // RegisterSpace (KEY FIX: Must be 1)
	);


	CD3DX12_ROOT_SIGNATURE_DESC rootDesc(
		(UINT)params.size(),
		params.data(),
		_countof(staticSamplers),
		staticSamplers
	);

	Microsoft::WRL::ComPtr<ID3DBlob> signature;
	Microsoft::WRL::ComPtr<ID3DBlob> error;
	HRESULT hr = D3D12SerializeRootSignature(&rootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error);

	if (FAILED(hr))
	{
		if (error) OutputDebugStringA((char*)error->GetBufferPointer());
		throw std::runtime_error("Failed to serialize NRD Root Sig");
	}

	if (FAILED(mDevice.Get()->CreateRootSignature(0, signature->GetBufferPointer(), signature->GetBufferSize(), IID_PPV_ARGS(&mNrdRootSignatures[index]))))
	{
		throw std::runtime_error("Failed to create NRD Root Signature");
	}
}

void Renderer::InitializeNRD()
{
	// 1. Setup Denoiser Descriptor
	// In NRD 4, you define a list of denoisers (methods) to include in the instance.
	nrd::DenoiserDesc denoiserDesc = {};
	denoiserDesc.identifier = 0; // Unique ID for this denoiser (you use this in GetComputeDispatches)
	denoiserDesc.denoiser = nrd::Denoiser::REBLUR_DIFFUSE_SPECULAR; // Choose denoiser type

	// Note: In NRD v4.4+, renderWidth/Height were removed from creation. 
	// Resolution is now handled strictly via CommonSettings per-frame.
	// If you are on NRD v4.0-v4.3, uncomment these:
	// denoiserDesc.renderWidth = mWidth;
	// denoiserDesc.renderHeight = mHeight;

	// 2. Setup Instance Descriptor
	nrd::InstanceCreationDesc instanceDesc = {};
	instanceDesc.denoisers = &denoiserDesc;
	instanceDesc.denoisersNum = 1;

	// 3. Create Instance
	if (nrd::CreateInstance(instanceDesc, mNrdInstance) != nrd::Result::SUCCESS)
	{
		throw std::runtime_error("Failed to create NRD Instance");
	}

	// 4. Create PSOs (The logic remains similar, but using the Instance)
	// GetInstanceDesc returns the description of all pipelines needed for the instance.
	const nrd::InstanceDesc* desc = nrd::GetInstanceDesc(*mNrdInstance);

	// Resize vectors to hold PSOs and Signatures
	mNrdPipelines.resize(desc->pipelinesNum);
	mNrdRootSignatures.resize(desc->pipelinesNum);

	for (uint32_t i = 0; i < desc->pipelinesNum; ++i)
	{
		const nrd::PipelineDesc& pipeDesc = desc->pipelines[i];

		// A. Create Root Signature (Use the helper from previous step)
		CreateNrdRootSignature(pipeDesc, i);

		// B. Create Compute PSO
		D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
		psoDesc.pRootSignature = mNrdRootSignatures[i].Get();
		psoDesc.CS = { pipeDesc.computeShaderDXIL.bytecode, pipeDesc.computeShaderDXIL.size };

		if (FAILED(mDevice.Get()->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&mNrdPipelines[i]))))
		{
			throw std::runtime_error("Failed to create NRD PSO");
		}
	}
}

void Renderer::DenoiseWithNRD(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj, const DirectX::XMFLOAT3& camPos)
{
	// =========================================================
	// 1. FILL COMMON SETTINGS
	// =========================================================
	nrd::CommonSettings common = {};

	// TRANSPOSE MATRICES (Row-Major -> Column-Major)
	DirectX::XMMATRIX viewT = view;
	DirectX::XMMATRIX projT = proj;

	// 1. View Matrix (World-to-View)
	memcpy(common.worldToViewMatrix, &viewT, sizeof(float) * 16);

	// 2. Projection Matrix (View-to-Clip)
	memcpy(common.viewToClipMatrix, &projT, sizeof(float) * 16);

	// 3. Previous Matrices (Required for Motion Vectors)
	// For now, we use static variables to hold the previous frame's matrices.
	// In a real engine, these should be member variables (mPrevView, mPrevProj).
	static DirectX::XMMATRIX prevViewT = viewT;
	static DirectX::XMMATRIX prevProjT = projT;

	memcpy(common.worldToViewMatrixPrev, &prevViewT, sizeof(float) * 16);
	memcpy(common.viewToClipMatrixPrev, &prevProjT, sizeof(float) * 16);

	// Update history for next frame
	prevViewT = viewT;
	prevProjT = projT;

	// 4. Resolution & Jitter
	common.resourceSize[0] = (uint16_t)mWidth;
	common.resourceSize[1] = (uint16_t)mHeight;
	common.rectSize[0] = (uint16_t)mWidth;
	common.rectSize[1] = (uint16_t)mHeight;

	common.resourceSizePrev[0] = (uint16_t)mWidth;
	common.resourceSizePrev[1] = (uint16_t)mHeight;
	common.rectSizePrev[0] = (uint16_t)mWidth;
	common.rectSizePrev[1] = (uint16_t)mHeight;

	// If you are NOT using TAA/DLSS, keep jitter at 0.0f
	common.cameraJitter[0] = 0.0f;
	common.cameraJitter[1] = 0.0f;

	// 5. Motion Vector Configuration
	// Your shader likely outputs UV motion (Screen Space).
	// If so, keep 'isMotionVectorInWorldSpace' = false.
	// If your MV texture contains pixel deltas (e.g. +10 pixels), scale by 1/resolution.
	common.isMotionVectorInWorldSpace = false;
	common.motionVectorScale[0] = (float)mWidth;
	common.motionVectorScale[1] = (float)mHeight;

	common.frameIndex = mFrameCount;
	common.accumulationMode = nrd::AccumulationMode::CONTINUE;

	nrd::SetCommonSettings(*mNrdInstance, common);

	//nrd::ReblurSettings reblurSettings = {};
	//reblurSettings.enableAntiFirefly = true;
	//reblurSettings.maxAccumulatedFrameNum = 30;
	//nrd::SetDenoiserSettings(*mNrdInstance, 0, &reblurSettings); // 0 = REBLUR_DIFFUSE_SPECULAR

	// =========================================================
	// 2. GET DISPATCHES
	// =========================================================
	const nrd::DispatchDesc* dispatches = nullptr;
	uint32_t dispatchCount = 0;
	const nrd::Identifier denoisers[] = { 0 }; // The ID we set in InitializeNRD (0)

	nrd::GetComputeDispatches(*mNrdInstance, denoisers, 1, dispatches, dispatchCount);

	// =========================================================
	// 3. EXECUTE DISPATCHES
	// =========================================================
	const nrd::InstanceDesc* instanceDesc = nrd::GetInstanceDesc(*mNrdInstance);
	ID3D12DescriptorHeap* heaps[] = { mFrameHeap.Get() };
	mCommandList.Get()->SetDescriptorHeaps(1, heaps);

	for (uint32_t i = 0; i < dispatchCount; ++i)
	{
		const nrd::DispatchDesc& d = dispatches[i];
		const nrd::PipelineDesc& pipeDesc = instanceDesc->pipelines[d.pipelineIndex];

		mCommandList.Get()->SetPipelineState(mNrdPipelines[d.pipelineIndex].Get());
		mCommandList.Get()->SetComputeRootSignature(mNrdRootSignatures[d.pipelineIndex].Get());

		// A. Bind Constants (Descriptor Table at Space 1)
		// We set up the Root Signature to expect a CBV Range at Space 1.
		if (d.constantBufferDataSize > 0)
		{
			// 1. Upload Data
			auto cbAlloc = mUploadHeap.Allocate(d.constantBufferDataSize);
			memcpy(cbAlloc.cpuPtr, d.constantBufferData, d.constantBufferDataSize);

			// 2. Allocate Descriptor
			auto cbvSlot = mFrameHeap.Allocate(1);

			// 3. Create CBV
			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc = {};
			cbvDesc.BufferLocation = mUploadHeap.GetResource()->GetGPUVirtualAddress() + cbAlloc.offset;
			cbvDesc.SizeInBytes = (d.constantBufferDataSize + 255) & ~255;
			mDevice.Get()->CreateConstantBufferView(&cbvDesc, cbvSlot.cpuHandle);

			// 4. Bind Table (Last Parameter)
			mCommandList.Get()->SetComputeRootDescriptorTable(pipeDesc.resourceRangesNum, cbvSlot.gpuHandle);
		}

		uint32_t resourceCursor = 0;

		// B. Bind Resources (SRV/UAV Tables)
		for (uint32_t r = 0; r < pipeDesc.resourceRangesNum; ++r)
		{
			const nrd::ResourceRangeDesc& range = pipeDesc.resourceRanges[r];
			auto tableAlloc = mFrameHeap.Allocate(range.descriptorsNum);

			UINT handleSize = mFrameHeap.GetIncrementSize();

			for (uint32_t dIndex = 0; dIndex < range.descriptorsNum; ++dIndex)
			{
				const nrd::ResourceDesc& resDesc = d.resources[resourceCursor++];
				nrd::ResourceType type = resDesc.type;
				D3D12_CPU_DESCRIPTOR_HANDLE src = { 0 };

				// --- MAP INPUTS ---
				if (type == nrd::ResourceType::IN_MV) src = mMotionVectorSrvCpuHandle;
				else if (type == nrd::ResourceType::IN_NORMAL_ROUGHNESS) src = mNormalRoughnessSrvCpuHandle;
				else if (type == nrd::ResourceType::IN_VIEWZ) src = mViewZSrvCpuHandle;

				// Split Radiance Inputs
				else if (type == nrd::ResourceType::IN_DIFF_RADIANCE_HITDIST) src = mRtDiffuseSrvCpuHandle;
				else if (type == nrd::ResourceType::IN_SPEC_RADIANCE_HITDIST) src = mRtSpecularSrvCpuHandle;

				// --- MAP OUTPUTS ---
				else if (type == nrd::ResourceType::OUT_DIFF_RADIANCE_HITDIST) src = mDenoisedDiffuseUavCpuHandle;
				else if (type == nrd::ResourceType::OUT_SPEC_RADIANCE_HITDIST) src = mDenoisedSpecularUavCpuHandle;

				// --- MAP POOLS (Scratch Memory) ---
				else if (type == nrd::ResourceType::TRANSIENT_POOL || type == nrd::ResourceType::PERMANENT_POOL)
				{
					// Check if the range expects a UAV (Storage) or SRV (Texture)
					bool isUAV = (range.descriptorType == nrd::DescriptorType::STORAGE_TEXTURE);

					// Use indexInPool from the ResourceDesc!
					NrdPoolEntry& entry = GetNrdPoolEntry(resDesc.indexInPool, type);
					src = isUAV ? entry.uavHandle : entry.srvHandle;
				}

				if (src.ptr != 0)
				{
					D3D12_CPU_DESCRIPTOR_HANDLE dst = tableAlloc.GetCpuHandle(dIndex, handleSize);
					mDevice.Get()->CopyDescriptorsSimple(1, dst, src, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
				}
			}
			mCommandList.Get()->SetComputeRootDescriptorTable(r, tableAlloc.gpuHandle);
		}

		mCommandList.Get()->Dispatch(d.gridWidth, d.gridHeight, 1);
	}

	// =========================================================
	// 4. RESTORE STATES
	// =========================================================

	// We need to put things back for the next frame:
	D3D12_RESOURCE_BARRIER cleanupBarriers[] =
	{
		CD3DX12_RESOURCE_BARRIER::Transition(
			mRtDiffuseResource.Get(),
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS
			),
		CD3DX12_RESOURCE_BARRIER::Transition(
			mRtSpecularResource.Get(),
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS
			),
		CD3DX12_RESOURCE_BARRIER::Transition(
			mNormalRoughnessTex.Get(),
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS
			),
		CD3DX12_RESOURCE_BARRIER::Transition(
			mViewZTex.Get(),
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS
			)
	};

	mCommandList.Get()->ResourceBarrier(_countof(cleanupBarriers), cleanupBarriers);
}

Renderer::NrdPoolEntry& Renderer::GetNrdPoolEntry(size_t index, nrd::ResourceType type)
{
	// 1. Select the correct vector based on the request type
	std::vector<NrdPoolEntry>& pool = (type == nrd::ResourceType::PERMANENT_POOL)
		? mPermanentPool
		: mTransientPool;

	// 2. Expand the vector if this index hasn't been allocated yet
	if (index >= pool.size())
		pool.resize(index + 1);

	if (pool[index].texture.resource.Get() == nullptr)
	{

		// 3. Create the Texture
		// NRD internal buffers generally require high precision (Float16/Float32).
		// R16G16B16A16_FLOAT is the safest default for NRD pools.
		D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			mWidth,
			mHeight,
			1, 1, 1, 0,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS // Must be a UAV
		);

		// Use your existing texture creation logic or raw D3D12
		GPUTexture tex;
		tex.format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		tex.width = mWidth;
		tex.height = mHeight;
		tex.resource.Initialize(mDevice.Get(), desc, CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT));

		// 4. Create Descriptors (SRV & UAV)
		// We allocate fresh slots in the heap for this new texture
		auto srvInfo = mCpuHeap.Allocate(1);
		auto uavInfo = mCpuHeap.Allocate(1);

		pool[index].texture = tex;
		pool[index].srvHandle = srvInfo.cpuHandle;
		pool[index].uavHandle = uavInfo.cpuHandle;

		// Create SRV (Shader Resource View)
		CreateTextureView(tex.resource.Get(), tex.format, pool[index].srvHandle, 1);

		// Create UAV (Unordered Access View)
		D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
		uavDesc.Format = tex.format;
		uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		mDevice.Get()->CreateUnorderedAccessView(tex.resource.Get(), nullptr, &uavDesc, pool[index].uavHandle);
	}

	// 5. Return the entry
	return pool[index];
}

void Renderer::InitializeCompositePipeLineState()
{
	HLSLShader cs = mShaderCompiler.CompileFromFile(L"Source/Shaders/CompositeCS.hlsl", L"cs_6_0");
	mCompositePipelineState.InitializeComposite(mDevice.Get(), std::move(cs));
}

void Renderer::DispatchComposite()
{
	// 1. Transitions
	// Denoised Textures: UAV (from NRD) -> SRV (for Composite Read)
	// BackBuffer: PRESENT -> UAV (for Composite Write)
	D3D12_RESOURCE_BARRIER barriers[] =
	{
		CD3DX12_RESOURCE_BARRIER::Transition(mDenoisedDiffuse.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		CD3DX12_RESOURCE_BARRIER::Transition(mDenoisedSpecular.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
		//CD3DX12_RESOURCE_BARRIER::Transition(mFinalColorOutput.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),

	};
	mCommandList.Get()->ResourceBarrier(_countof(barriers), barriers);

	// 2. Setup Pipeline
	mCommandList.Get()->SetPipelineState(mCompositePipelineState.Get());
	mCommandList.Get()->SetComputeRootSignature(mCompositePipelineState.GetRootSignature());

	// 3. Bind Descriptors
	// We assume Root Sig is: [0]: UAV Table (Output), [1]: SRV Table (Inputs)

	// Alloc UAV for Output (Backbuffer)
	auto uavAlloc = mFrameHeap.Allocate(1);
	CreateUAV(mFinalColorOutput.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, uavAlloc.cpuHandle);
	mCommandList.Get()->SetComputeRootDescriptorTable(0, uavAlloc.gpuHandle);

	// Alloc SRVs for Inputs (Denoised Diff, Denoised Spec, Albedo)
	auto srvAlloc = mFrameHeap.Allocate(3);
	UINT inc = mFrameHeap.GetIncrementSize();

	mDevice.Get()->CopyDescriptorsSimple(1, srvAlloc.GetCpuHandle(0, inc), mDenoisedDiffuseSrvCpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	mDevice.Get()->CopyDescriptorsSimple(1, srvAlloc.GetCpuHandle(1, inc), mDenoisedSpecularSrvCpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
	mDevice.Get()->CopyDescriptorsSimple(1, srvAlloc.GetCpuHandle(2, inc), mAlbedoSrvCpuHandle, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	mCommandList.Get()->SetComputeRootDescriptorTable(1, srvAlloc.gpuHandle);

	// 4. Dispatch
	// 8x8 thread group size
	mCommandList.Get()->Dispatch((mWidth + 7) / 8, (mHeight + 7) / 8, 1);

	// 5. Copy to Back Buffer
	D3D12_RESOURCE_BARRIER copyBarriers[]
	{
		// Final Color Output: UAV -> COPY_SOURCE
		CD3DX12_RESOURCE_BARRIER::Transition(mFinalColorOutput.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
		// Backbuffer: PRESENT -> COPY_DEST
		CD3DX12_RESOURCE_BARRIER::Transition(mSwapChain.GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST)
	};
	mCommandList.Get()->ResourceBarrier(_countof(copyBarriers), copyBarriers);
	mCommandList.Get()->CopyResource(mSwapChain.GetCurrentBackBuffer(), mFinalColorOutput.Get());

	// 6. Cleanup Transitions
	D3D12_RESOURCE_BARRIER postBarriers[] = {
		// BackBuffer: Copy Dest -> Present
		CD3DX12_RESOURCE_BARRIER::Transition(mSwapChain.GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PRESENT),
		// Inputs: SRV -> UAV (for next frame)
		CD3DX12_RESOURCE_BARRIER::Transition(mDenoisedDiffuse.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		CD3DX12_RESOURCE_BARRIER::Transition(mDenoisedSpecular.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		// Intermediate: Copy Source -> Common (Ready for next frame)
		CD3DX12_RESOURCE_BARRIER::Transition(mFinalColorOutput.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
	};
	mCommandList.Get()->ResourceBarrier(_countof(postBarriers), postBarriers);
}

void Renderer::CreateUAV(ID3D12Resource* pResource, DXGI_FORMAT format, D3D12_CPU_DESCRIPTOR_HANDLE handle)
{
	CD3DX12_UNORDERED_ACCESS_VIEW_DESC uavDesc{};
	uavDesc.Format = format;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	mDevice.Get()->CreateUnorderedAccessView(pResource, nullptr, &uavDesc, handle);
}

void Renderer::CreateRayTracingOutput()
{
	constexpr DXGI_FORMAT rtOutputFormat = DXGI_FORMAT_R16G16B16A16_FLOAT;
	// 1. Diffuse Radiance (Was mRtOutputResource)
	// Needs R16G16B16A16_FLOAT for Radiance + HitDist
	{
		D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R16G16B16A16_FLOAT, mWidth, mHeight, 1, 1, 1, 0,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		mRtDiffuseResource.Initialize(mDevice.Get(), desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		mRtDiffuseResource.Get()->SetName(L"RT Diffuse Output");
	}

	// 2. Specular Radiance (NEW)
	{
		D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R16G16B16A16_FLOAT, mWidth, mHeight, 1, 1, 1, 0,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		mRtSpecularResource.Initialize(mDevice.Get(), desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		mRtSpecularResource.Get()->SetName(L"RT Specular Output");
	}

	// 3. Normal + Roughness (u1)
	// NRD recommends high precision for Normals. R10G10B10A2 or R16G16B16A16_FLOAT.
	{
		D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			mWidth,
			mHeight,
			1, // array size
			1,  // mip levels
			1, // sample count
			0, // sample quality
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);

		mNormalRoughnessTex.Initialize(mDevice.Get(), desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		mNormalRoughnessTex.Get()->SetName(L"Normal + Roughness Texture");
	}

	// 4. ViewZ (u2) - MUST be FLOAT format (R32_FLOAT)
	{
		D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R32_FLOAT,
			mWidth,
			mHeight,
			1, // array size
			1,  // mip levels
			1, // sample count
			0, // sample quality
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);
		mViewZTex.Initialize(mDevice.Get(), desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		mViewZTex.Get()->SetName(L"ViewZ Texture");
	}

	// 5. Albedo Texture (u3) 
	{
		D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R8G8B8A8_UNORM,
			mWidth,
			mHeight,
			1, // array size
			1,  // mip levels
			1, // sample count
			0, // sample quality
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);
		mAlbedoTex.Initialize(mDevice.Get(), desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		mAlbedoTex.Get()->SetName(L"Albedo Texture");
	}

	// 6. Denoised Diffuse Output (NRD)
	{
		D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			mWidth,
			mHeight,
			1, // array size
			1,  // mip levels
			1, // sample count
			0, // sample quality
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);
		mDenoisedDiffuse.Initialize(mDevice.Get(), desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		mDenoisedDiffuse.Get()->SetName(L"Denoised Diffuse Texture");
	}

	// 7. Denoised Specular Output (NRD)
	{
		D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			mWidth,
			mHeight,
			1, // array size
			1,  // mip levels
			1, // sample count
			0, // sample quality
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);
		mDenoisedSpecular.Initialize(mDevice.Get(), desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		mDenoisedSpecular.Get()->SetName(L"Denoised Specular Texture");
	}

	// 8. Final Composite Output
	{
		D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R8G8B8A8_UNORM,
			mWidth, mHeight,
			1, 1, 1, 0,
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
		);

		mFinalColorOutput.Initialize(mDevice.Get(), desc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		mFinalColorOutput.Get()->SetName(L"Final Color Output");
	}

	// --- Create UAV Descriptors ---
	// We need 3 CONTIGUOUS descriptors in the heap for the table
	auto uavTable = mSrvHeap.Allocate(5);
	mRtDiffuseUavCpuHandle = uavTable.cpuHandle;
	mRtDiffuseUavGpuHandle = uavTable.gpuHandle;

	UINT inc = mSrvHeap.GetIncrementSize();

	//  Create UAVs
	// Slot 0: Diffuse
	CreateUAV(mRtDiffuseResource.Get(), rtOutputFormat, uavTable.GetCpuHandle(0, inc));
	// Slot 1: Specular
	CreateUAV(mRtSpecularResource.Get(), rtOutputFormat, uavTable.GetCpuHandle(1, inc));
	// Slot 2: Normal/Roughness
	CreateUAV(mNormalRoughnessTex.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, uavTable.GetCpuHandle(2, inc));
	// Slot 3: ViewZ
	CreateUAV(mViewZTex.Get(), DXGI_FORMAT_R32_FLOAT, uavTable.GetCpuHandle(3, inc));
	// Slot 4: Albedo
	CreateUAV(mAlbedoTex.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, uavTable.GetCpuHandle(4, inc));


	// --- Create SRV Descriptors (For NRD Input) ---
	auto srvTable = mCpuHeap.Allocate(7);

	// NRD Input SRVs
	// 1. Diffuse Radiance + HitDist
	mRtDiffuseSrvCpuHandle = srvTable.GetCpuHandle(0, inc);
	CreateTextureView(mRtDiffuseResource.Get(), rtOutputFormat, mRtDiffuseSrvCpuHandle, 1);

	// 2. Specular Radiance + HitDist
	mRtSpecularSrvCpuHandle = srvTable.GetCpuHandle(1, inc);
	CreateTextureView(mRtSpecularResource.Get(), rtOutputFormat, mRtSpecularSrvCpuHandle, 1);

	// 2. Normal + Roughness
	mNormalRoughnessSrvCpuHandle = srvTable.GetCpuHandle(2, inc);
	CreateTextureView(mNormalRoughnessTex.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, mNormalRoughnessSrvCpuHandle, 1);

	// 3. ViewZ
	mViewZSrvCpuHandle = srvTable.GetCpuHandle(3, inc);
	CreateTextureView(mViewZTex.Get(), DXGI_FORMAT_R32_FLOAT, mViewZSrvCpuHandle, 1);


	// Composite Inputs (Denoised SRVs)
	// 4. Denoised Diffuse
	mDenoisedDiffuseSrvCpuHandle = srvTable.GetCpuHandle(4, inc);
	CreateTextureView(mDenoisedDiffuse.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, mDenoisedDiffuseSrvCpuHandle, 1);

	// 5. Denoised Specular
	mDenoisedSpecularSrvCpuHandle = srvTable.GetCpuHandle(5, inc);
	CreateTextureView(mDenoisedSpecular.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, mDenoisedSpecularSrvCpuHandle, 1);

	// 6. Albedo
	mAlbedoSrvCpuHandle = srvTable.GetCpuHandle(6, inc);
	CreateTextureView(mAlbedoTex.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, mAlbedoSrvCpuHandle, 1);

	// Create UAVs for NRD Denoise Output
	auto nrdOutAlloc = mCpuHeap.Allocate(2);
	// 1. Denoised Diffuse
	mDenoisedDiffuseUavCpuHandle = nrdOutAlloc.GetCpuHandle(0, inc);
	CreateUAV(mDenoisedDiffuse.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, mDenoisedDiffuseUavCpuHandle);

	// 2. Denoised Specular
	mDenoisedSpecularUavCpuHandle = nrdOutAlloc.GetCpuHandle(1, inc);
	CreateUAV(mDenoisedSpecular.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, mDenoisedSpecularUavCpuHandle);
}

void Renderer::CreateRayTracingPipeline()
{
	// --- 1. GLOBAL Root Signature (Output, TLAS, Camera) ---
	CD3DX12_DESCRIPTOR_RANGE uavRange(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 5, 0);
	CD3DX12_ROOT_PARAMETER globalParams[3]{};
	globalParams[0].InitAsDescriptorTable(1, &uavRange);
	globalParams[1].InitAsShaderResourceView(0);
	globalParams[2].InitAsConstantBufferView(0);

	CD3DX12_STATIC_SAMPLER_DESC staticSampler(
		0, // ShaderRegister (s0)
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP
	);

	CD3DX12_ROOT_SIGNATURE_DESC globalRootDesc(_countof(globalParams), globalParams, 1, &staticSampler);
	globalRootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_NONE;

	Microsoft::WRL::ComPtr<ID3DBlob> blob, error;
	D3D12SerializeRootSignature(&globalRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &blob, &error);
	mDevice.Get()->CreateRootSignature(0, blob->GetBufferPointer(), blob->GetBufferSize(), IID_PPV_ARGS(mRtGlobalRootSignature.ReleaseAndGetAddressOf()));

	// --- 2. LOCAL Root Signature (Material, VB, IB) ---
	// These arguments are passed via the SBT (Shader Binding Table)
	CD3DX12_ROOT_PARAMETER localParams[4];
	localParams[0].InitAsConstantBufferView(0, 1); // b0, space1 (Material CB)
	localParams[1].InitAsShaderResourceView(0, 1); // t0, space1 (Vertices)
	localParams[2].InitAsShaderResourceView(1, 1); // t1, space1 (Indices)

	CD3DX12_DESCRIPTOR_RANGE texRange(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, Config::cNumberOfTextureSlots, 2, 1); // t2-tN+2, space1
	localParams[3].InitAsDescriptorTable(1, &texRange);

	CD3DX12_ROOT_SIGNATURE_DESC localRootDesc(_countof(localParams), localParams);
	localRootDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;

	Microsoft::WRL::ComPtr<ID3DBlob> localBlob;
	D3D12SerializeRootSignature(&localRootDesc, D3D_ROOT_SIGNATURE_VERSION_1, &localBlob, &error);

	// Note: You need a ComPtr member for this in Renderer.h to keep it alive!
	// ComPtr<ID3D12RootSignature> mRtLocalRootSignature;
	mDevice.Get()->CreateRootSignature(0, localBlob->GetBufferPointer(), localBlob->GetBufferSize(), IID_PPV_ARGS(mRtLocalRootSignature.ReleaseAndGetAddressOf()));

	// --- 3. Load Shader ---
	auto rtShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/RayTracing.hlsl", L"lib_6_3", {}, L"");
	auto shaderBlob = rtShader.GetShaderBlob();

	// --- 4. State Object ---
	CD3DX12_STATE_OBJECT_DESC rtPipe(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

	auto lib = rtPipe.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
	D3D12_SHADER_BYTECODE libdxil = { shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize() };
	lib->SetDXILLibrary(&libdxil);
	lib->DefineExport(L"MyRayGen");
	lib->DefineExport(L"MyMiss");
	lib->DefineExport(L"MyShadowMiss");
	//lib->DefineExport(L"MyClosestHit");
	lib->DefineExport(L"MyClosestHitOpaque");
	lib->DefineExport(L"MyClosestHitTransparent");
	lib->DefineExport(L"MyAnyHit"); // Export the AnyHit shader

	// Hit Group 0 (Opaque) -> Just Closest Hit
	auto hitGroup0 = rtPipe.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
	hitGroup0->SetClosestHitShaderImport(L"MyClosestHitOpaque");
	hitGroup0->SetHitGroupExport(L"HitGroup0");
	hitGroup0->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

	// Hit Group 1 (Masked) -> Closest Hit + Any Hit (for alpha test)
	auto hitGroup1 = rtPipe.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
	hitGroup1->SetClosestHitShaderImport(L"MyClosestHitOpaque");
	hitGroup1->SetAnyHitShaderImport(L"MyAnyHit");
	hitGroup1->SetHitGroupExport(L"HitGroup1");
	hitGroup1->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

	// Hit Group 2 (Transparent) -> Currently same as Masked
	auto hitGroup2 = rtPipe.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
	hitGroup2->SetClosestHitShaderImport(L"MyClosestHitTransparent");
	//hitGroup2->SetHitGroupExport(L"HitGroup1"); // Reuse HitGroup1 export for now
	// Note: If you want separate logic, create "HitGroup2" export above
	hitGroup2->SetHitGroupExport(L"HitGroup2");
	hitGroup2->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

	// Shader Config
	auto shaderConfig = rtPipe.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
	// Payload Size: 
	// 12 (Diffuse Radiance) + 12 (Specular Radiance) + 4 (Hit Distance) + 4 (Recursion Depth) + 12 (Normal)  + 4 (Roughness) = 48 bytes
	// Attributes: 8 bytes (barycentrics)
	shaderConfig->Config(48, sizeof(float) * 2);

	// Local Root Signature Association
	// We must tell the pipeline that HitGroup0 and HitGroup1 use the Local Root Sig
	auto localRootSub = rtPipe.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
	localRootSub->SetRootSignature(mRtLocalRootSignature.Get());

	auto association = rtPipe.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
	association->SetSubobjectToAssociate(*localRootSub);
	association->AddExport(L"HitGroup0");
	association->AddExport(L"HitGroup1");
	association->AddExport(L"HitGroup2");

	// Global Root Signature Association
	auto globalRoot = rtPipe.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
	globalRoot->SetRootSignature(mRtGlobalRootSignature.Get());

	// Pipeline Config
	auto pipelineConfig = rtPipe.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG1_SUBOBJECT>();
	pipelineConfig->Config(8, D3D12_RAYTRACING_PIPELINE_FLAG_SKIP_PROCEDURAL_PRIMITIVES); // Max recursion depth = 8


	HRESULT hr = mDevice.Get()->CreateStateObject(rtPipe, IID_PPV_ARGS(mRtStateObject.ReleaseAndGetAddressOf()));
	if (FAILED(hr)) throw std::runtime_error("Failed to create RTPSO");
}

void Renderer::CreateShaderBindingTable()
{
	Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> props;
	HRESULT hr = mRtStateObject.As(&props);
	if (FAILED(hr)) throw std::runtime_error("Failed to get RTPSO properties");

	// 1. Get Shader Identifiers
	void* rayGenID = props->GetShaderIdentifier(L"MyRayGen");
	void* missID = props->GetShaderIdentifier(L"MyMiss");
	void* shadowMissID = props->GetShaderIdentifier(L"MyShadowMiss");
	void* hitGroup0ID = props->GetShaderIdentifier(L"HitGroup0"); // Opaque
	void* hitGroup1ID = props->GetShaderIdentifier(L"HitGroup1"); // Masked
	void* hitGroup2ID = props->GetShaderIdentifier(L"HitGroup2"); // Transparent

	// 2. Calculate Layout
	UINT shaderIDSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES; // 32
	// Record = ID (32) + MaterialPtr (8) + VertexPtr (8) + IndexPtr (8) = 56
	// Align to 32 bytes -> 64 bytes
	UINT recordSize = 64;
	mSbtEntrySize = recordSize;

	// 3. Gather Meshes & Upload Material Data
	// We must process meshes in the EXACT same order as BuildTLAS!
	std::vector<MeshGpuData*> allMeshes;
	auto Collect = [&](std::vector<MeshGpuData>& list) { for (auto& m : list) allMeshes.push_back(&m); };
	Collect(mOpaqueSingleSidedMeshes);
	Collect(mOpaqueDoubleSidedMeshes);
	Collect(mMaskedSingleSidedMeshes);
	Collect(mMaskedDoubleSidedMeshes);
	Collect(mTransparentMeshes);

	// FIX: Constant Buffer addresses MUST be multiples of 256 bytes for binding
	UINT materialStride = (sizeof(MeshMaterialData) + 255) & ~255;
	UINT materialBufferSize = (UINT)allMeshes.size() * materialStride;

	// Ensure mMaterialBuffer exists (Add to Renderer.h!)
	if (mMaterialBuffer.Get() == nullptr || mMaterialBuffer.Get()->GetDesc().Width < materialBufferSize)
	{
		mMaterialBuffer.Initialize(mDevice.Get(), materialBufferSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);
	}

	// Write Material Data
	uint8_t* pMatData;
	mMaterialBuffer.Get()->Map(0, nullptr, (void**)&pMatData);
	D3D12_GPU_VIRTUAL_ADDRESS matBaseAddr = mMaterialBuffer.Get()->GetGPUVirtualAddress();

	for (size_t i = 0; i < allMeshes.size(); ++i)
	{
		// Copy data to aligned offset
		memcpy(pMatData + (i * materialStride), &allMeshes[i]->materialData, sizeof(MeshMaterialData));
	}
	mMaterialBuffer.Get()->Unmap(0, nullptr);

	// 4. Build SBT
	UINT numHitRecords = (UINT)allMeshes.size();
	//UINT sbtSize = 32 + 32 + (numHitRecords * recordSize);
	UINT sbtSize = mSbtEntrySize + 2 * mSbtEntrySize + (numHitRecords * mSbtEntrySize);
	sbtSize = (sbtSize + 255) & ~255; // Align buffer

	mSbtResource.Initialize(mDevice.Get(), sbtSize, D3D12_HEAP_TYPE_UPLOAD, D3D12_RESOURCE_STATE_GENERIC_READ);

	uint8_t* pData;
	mSbtResource.Get()->Map(0, nullptr, (void**)&pData);

	// Entry 0: RayGen (No args)
	memcpy(pData, rayGenID, shaderIDSize);
	//pData += 32;

	// Entry 1: Miss (No args)
	// Record 0: Miss Shader
	uint8_t* pMiss = pData + mSbtEntrySize;
	memcpy(pMiss, missID, shaderIDSize);

	// Record 1: Shadow Miss Shader
	uint8_t* pShadowMiss = pMiss + mSbtEntrySize;
	memcpy(pShadowMiss, shadowMissID, shaderIDSize);

	uint8_t* pHitGroupStart = pShadowMiss + mSbtEntrySize;

	// Entry 2..N: Hit Groups
	for (size_t i = 0; i < numHitRecords; ++i)
	{
		MeshGpuData* mesh = allMeshes[i];
		uint8_t* pRecord = pHitGroupStart + (i * mSbtEntrySize);

		// --- FIX: ASSIGN CORRECT HIT GROUP ID BASED ON RENDER LAYER ---
		void* currentID = hitGroup0ID;

		// This inference MUST match the order in the 'Collect' lambda above.
		// Opaque (Single/Double) -> HG0
		// Masked/Transparent -> HG1 or HG2
		if (i >= mOpaqueSingleSidedMeshes.size() + mOpaqueDoubleSidedMeshes.size())
		{
			// If it's Masked or Transparent, use HG1/HG2
			if (i < mOpaqueSingleSidedMeshes.size() + mOpaqueDoubleSidedMeshes.size() + mMaskedSingleSidedMeshes.size() + mMaskedDoubleSidedMeshes.size())
			{
				currentID = hitGroup1ID; // Masked
			}
			else
			{
				currentID = hitGroup2ID; // Transparent
			}
		}

		// Copy Shader ID
		memcpy(pRecord, currentID, shaderIDSize);
		//pRecord += shaderIDSize;
		uint8_t* pArgs = pRecord + shaderIDSize;

		D3D12_GPU_DESCRIPTOR_HANDLE texHandle = mesh->materialTable.gpuHandle;

		// Copy Arguments
		// 1. Material CBV Address (Aligned to 256 bytes)
		D3D12_GPU_VIRTUAL_ADDRESS matAddr = matBaseAddr + (i * materialStride);
		// 2. Vertex Buffer Address
		D3D12_GPU_VIRTUAL_ADDRESS vbAddr = mesh->vb.Get()->GetGPUVirtualAddress();
		// 3. Index Buffer Address
		D3D12_GPU_VIRTUAL_ADDRESS ibAddr = mesh->ib.Get()->GetGPUVirtualAddress();

		memcpy(pArgs, &matAddr, 8);
		memcpy(pArgs + 8, &vbAddr, 8);
		memcpy(pArgs + 16, &ibAddr, 8);

		memcpy(pArgs + 24, &texHandle, 8); // Texture SRV Table GPU Handle

		//pData += recordSize;
	}

	mSbtResource.Get()->Unmap(0, nullptr);
}


void Renderer::RenderRayTracing(const DirectX::XMMATRIX& viewProj, const DirectX::XMFLOAT3& camPos, const DirectX::XMFLOAT3& camForward)
{
	auto cmdList = mCommandList.Get();

	std::vector<D3D12_RESOURCE_BARRIER> preTraceBarriers;

	// 1. Bind Pipeline & Resources
	ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
	cmdList->SetDescriptorHeaps(1, heaps);

	cmdList->SetPipelineState1(mRtStateObject.Get());
	cmdList->SetComputeRootSignature(mRtGlobalRootSignature.Get());

	// Slot 0: UAV Table (Output Texture)
	cmdList->SetComputeRootDescriptorTable(0, mRtDiffuseUavGpuHandle);

	// Slot 1: TLAS (SRV)
	cmdList->SetComputeRootShaderResourceView(1, mTLAS.Get()->GetGPUVirtualAddress());

	// Slot 2: Camera CB (Persistent Buffer Update)
	RayGenConstantBuffer cb;
	cb.viewProjInverse = DirectX::XMMatrixInverse(nullptr, viewProj);
	cb.cameraPos = { camPos.x, camPos.y, camPos.z, 1.0f };
	cb.cameraForward = { camForward.x, camForward.y, camForward.z, 0.0f };
	cb.numLights = mConstantBufferData.numLights;
	cb.frameCount = mConstantBufferData.frameCount;
	memcpy(cb.lights, mConstantBufferData.lights, sizeof(LightData) * cb.numLights);

	const UINT frameIndex = mSwapChain.GetCurrentBackBufferIndex();
	const UINT64 rtCbOffset = static_cast<UINT64>(frameIndex) * mRtConstantBufferStride;
	void* pData;
	HRESULT hr = mRtConstantBuffer.Get()->Map(0, nullptr, &pData);
	uint8_t* mappedPtr = reinterpret_cast<uint8_t*>(pData);
	memcpy(mappedPtr + rtCbOffset, &cb, sizeof(RayGenConstantBuffer));
	mRtConstantBuffer.Get()->Unmap(0, nullptr);

	cmdList->SetComputeRootConstantBufferView(2, mRtConstantBuffer.Get()->GetGPUVirtualAddress());

	// 2. Dispatch Rays
	D3D12_DISPATCH_RAYS_DESC desc = {};
	desc.RayGenerationShaderRecord.StartAddress = mSbtResource.Get()->GetGPUVirtualAddress();
	desc.RayGenerationShaderRecord.SizeInBytes = mSbtEntrySize;

	desc.MissShaderTable.StartAddress = mSbtResource.Get()->GetGPUVirtualAddress() + mSbtEntrySize;
	desc.MissShaderTable.SizeInBytes = mSbtEntrySize * 2;
	desc.MissShaderTable.StrideInBytes = mSbtEntrySize;

	desc.HitGroupTable.StartAddress = mSbtResource.Get()->GetGPUVirtualAddress() + mSbtEntrySize * 3;
	// HitGroup table size = RecordSize * TotalMeshes
	// (Wa¿ne: rozmiar musi obejmowaæ wszystkie rekordy geometrii!)
	UINT numMeshes = static_cast<UINT>(
		mOpaqueSingleSidedMeshes.size() + mOpaqueDoubleSidedMeshes.size() +
		mMaskedSingleSidedMeshes.size() + mMaskedDoubleSidedMeshes.size());

	desc.HitGroupTable.SizeInBytes = mSbtEntrySize * numMeshes;
	desc.HitGroupTable.StrideInBytes = mSbtEntrySize;

	desc.Width = mWidth;
	desc.Height = mHeight;
	desc.Depth = 1;

	cmdList->DispatchRays(&desc);

	// Transition Ray Tracing Outputs (UAVs) to SRVs for the Denoising pass
	D3D12_RESOURCE_BARRIER postRtBarriers[] =
	{
		CD3DX12_RESOURCE_BARRIER::Transition(
			mRtDiffuseResource.Get(),
			D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
			D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
		),
			// 2. Specular Output (IN_SPEC_RADIANCE_HITDIST): UAV -> SRV
			CD3DX12_RESOURCE_BARRIER::Transition(
				mRtSpecularResource.Get(),
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
			),
			// 2. Normal + Roughness (IN_NORMAL_ROUGHNESS): UAV -> SRV
			CD3DX12_RESOURCE_BARRIER::Transition(
				mNormalRoughnessTex.Get(),
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
			),
			// 3. ViewZ (IN_VIEWZ): UAV -> SRV
			CD3DX12_RESOURCE_BARRIER::Transition(
				mViewZTex.Get(),
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE
			)
	};

	cmdList->ResourceBarrier(_countof(postRtBarriers), postRtBarriers);
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

void Renderer::Update(const DirectX::XMMATRIX& view, const DirectX::XMMATRIX& proj, const DirectX::XMFLOAT3& cameraPos, const DirectX::XMFLOAT3& cameraForward)
{
	// compute delta time
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	double dt = static_cast<double>(now.QuadPart - mPrevCounter.QuadPart) * mSecondsPerCount;
	mPrevCounter = now;

	DirectX::XMMATRIX viewProj = DirectX::XMMatrixMultiply(view, proj);
	mConstantBufferData.vpMatrix = viewProj;
	mConstantBufferData.prevVpMatrix = mPrevViewProj;
	mConstantBufferData.viewPos = DirectX::XMFLOAT4(cameraPos.x, cameraPos.y, cameraPos.z, 1.0f);
	mConstantBufferData.frameCount = mFrameCount++;

	// --- use cached static lights, avoid re-scanning models each frame ---
	const int staticCount = static_cast<int>(std::min<size_t>(mStaticLights.size(), cMaxLights));

	// light camera light
	const float cameraLightIntensity = 0.5f;
	if (staticCount < cMaxLights)
	{
		//LightData camLight{};
		//camLight.position = DirectX::XMFLOAT4(
		//    cameraPos.x + cameraForward.x * 1000.0f,
		//    cameraPos.y + cameraForward.y * 1000.0f,
		//    cameraPos.z + cameraForward.z * 1000.0f,
		//    1.0f);
		//camLight.diffuseColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, cameraLightIntensity);
		//camLight.specularColor = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, cameraLightIntensity);
		//camLight.dirType = DirectX::XMFLOAT4(cameraForward.x, cameraForward.y, cameraForward.z, 1.0f); // directional flag
		//mConstantBufferData.lights[staticCount] = camLight;
		//mConstantBufferData.numLights = staticCount + 1;

		LightData sunLight{};
		sunLight.position = DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
		sunLight.diffuseColor = DirectX::XMFLOAT4(1.0f, 0.95f, 0.9f, cameraLightIntensity);
		sunLight.specularColor = DirectX::XMFLOAT4(1.0f, 0.95f, 0.9f, cameraLightIntensity);

		sunLight.dirType = DirectX::XMFLOAT4(0.2f, -1.0f, 0.2f, 1.0f); // directional flag

		mConstantBufferData.lights[staticCount] = sunLight;
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
		mFrameHeap.Reset(); // Reset frame descriptor heap
		mUploadHeap.Reset(); // Reset upload heap
		RenderMotionVectors();
		RenderRayTracing(viewProj, cameraPos, cameraForward);
		DenoiseWithNRD(view, proj, cameraPos);
		DispatchComposite();
	}
	else
	{
		// Sort transparent meshes back-to-front each frame (temporary solution)
		SortTransparentMeshes(cameraPos);

		// Bind descriptor heap
		ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
		mCommandList.Get()->SetDescriptorHeaps(_countof(heaps), heaps);
		D3D12_RESOURCE_BARRIER barriers[2]{};
		// Transition depth buffer to DEPTH_WRITE
		barriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
			mDepthBuffer.GetResource(),
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_DEPTH_WRITE);
		barriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
			mSwapChain.GetCurrentBackBuffer(),
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET);

		mCommandList.Get()->ResourceBarrier(_countof(barriers), barriers);

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
		for (const auto& mesh : mMaskedSingleSidedMeshes)
			DrawMesh(mesh);

		// 4. Masked double-sided
		mCommandList.Get()->SetPipelineState(mPipelineStateMaskedDouble.Get());
		for (const auto& mesh : mMaskedDoubleSidedMeshes)
			DrawMesh(mesh);

		// 5. Transparent
		mCommandList.Get()->SetPipelineState(mPipelineStateTransparent.Get());
		for (const auto& mesh : mTransparentMeshes)
			DrawMesh(mesh);

		D3D12_RESOURCE_BARRIER cleanupBarriers[2]{};

		// 1. BackBuffer: RENDER_TARGET -> PRESENT (Existing)
		cleanupBarriers[0] = CD3DX12_RESOURCE_BARRIER::Transition(
			mSwapChain.GetCurrentBackBuffer(),
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT);

		// 2. DepthBuffer: DEPTH_WRITE -> COMMON (NEW!)
		// This ensures the Depth Buffer is in COMMON for the start of the next frame
		// (whether it be another Raster frame or a Ray Tracing frame).
		cleanupBarriers[1] = CD3DX12_RESOURCE_BARRIER::Transition(
			mDepthBuffer.GetResource(),
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			D3D12_RESOURCE_STATE_COMMON);

		mCommandList.Get()->ResourceBarrier(_countof(cleanupBarriers), cleanupBarriers);

	}
	mPrevViewProj = viewProj;


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
////////