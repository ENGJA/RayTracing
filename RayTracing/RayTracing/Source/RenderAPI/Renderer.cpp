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

static sl::float4x4 XMMatrixToSLFloat4x4(const DirectX::XMMATRIX& mat)
{
	sl::float4x4 result;
	DirectX::XMFLOAT4X4 temp;
	DirectX::XMStoreFloat4x4(&temp, mat);

	// Streamline uses row-major matrices
	for (int row = 0; row < 4; row++)
	{
		result[row].x = temp.m[row][0];
		result[row].y = temp.m[row][1];
		result[row].z = temp.m[row][2];
		result[row].w = temp.m[row][3];
	}
	return result;
}

/**
* @brief Maps TextureType enum to descriptor slot index.
*/
static int TextureTypeToSlot(TextureType type)
{
	return static_cast<int>(type);
}

static DXGI_FORMAT MakeSRGB(DXGI_FORMAT format)
{
	switch (format)
	{
	case DXGI_FORMAT_R8G8B8A8_UNORM: return DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
	case DXGI_FORMAT_BC1_UNORM: return DXGI_FORMAT_BC1_UNORM_SRGB;
	case DXGI_FORMAT_BC2_UNORM: return DXGI_FORMAT_BC2_UNORM_SRGB;
	case DXGI_FORMAT_BC3_UNORM: return DXGI_FORMAT_BC3_UNORM_SRGB;
	case DXGI_FORMAT_BC7_UNORM: return DXGI_FORMAT_BC7_UNORM_SRGB;
	default: return format;
	}
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
				bool isDDS = fullPath.ends_with(".dds") || fullPath.ends_with(".DDS");
				if (isDDS)
					continue; // DDS textures are loaded directly on the GPU
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

void Renderer::CreateMaterial(const Mesh& mesh, const string& directory, MeshGpuData& gpuData, const function<void()>& executeBatch, DirectX::ResourceUploadBatch& ddsBatch)
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

				if (!loadState.gpuTexture.resource.Get())
				{
					bool isSRGB = (slotIndex == 0 || slotIndex == 4); // Albedo or Emissive
					bool isDDS = fullPath.ends_with(".dds") || fullPath.ends_with(".DDS");
					if (isDDS)
					{
						loadState.gpuTexture = mTextureLoader.CreateTextureFromDDSPath(wstring(fullPath.begin(), fullPath.end()), ddsBatch);
						//if (isSRGB)
							//loadState.gpuTexture.format = MakeSRGB(loadState.gpuTexture.format);
					}
					else
					{
						if (loadState.decodeFuture.valid())
							loadState.decodedImage = loadState.decodeFuture.get();

						loadState.gpuTexture = mTextureLoader.CreateTextureFromDecodedImage(loadState.decodedImage, executeBatch, isSRGB);
						loadState.decodedImage = {};
					}
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

void Renderer::UploadSingleMesh(const Mesh& mesh, const string& directory, const function<void()>& executeBatch, DirectX::ResourceUploadBatch& ddsBatch)
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

	CreateMaterial(mesh, directory, gpu, executeBatch, ddsBatch);

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
		if (mesh.mDoubleSided)
			mTransparentDoubleSidedMeshes.push_back(std::move(gpu));
		else
			mTransparentSingleSidedMeshes.push_back(std::move(gpu));
		break;
	default:
		break;
	}
}

void Renderer::UploadMeshes(const function<void()>& executeBatch, DirectX::ResourceUploadBatch& ddsBatch)
{
	for (const auto& modelPtr : mModels)
	{
		for (const Mesh& mesh : modelPtr->mMeshes)
			UploadSingleMesh(mesh, modelPtr->mDirectory, executeBatch, ddsBatch);
	}
}

void Renderer::BuildMeshGpuData()
{
	mUploadHeap.Reset();
	mCommandList.ResetCommandList(0); // Use allocator 0 for one-time upload, not swap chain index
	DirectX::ResourceUploadBatch ddsBatch(mDevice.Get());
	ddsBatch.Begin();

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
	UploadMeshes(executeBatch, ddsBatch);
	executeBatch();

	auto ddsUploadFuture = ddsBatch.End(mCommandQueue.Get());
	ddsUploadFuture.wait();

	mCommandList.Get()->Close();
}

void Renderer::CollectStaticLights()
{
	mStaticLights.clear();
	mSunIndex = -1;
	vector<LightData> dirLights;
	for (const auto& modelPtr : mModels)
	{
		if (!modelPtr)
			continue;

		for (const auto& l : modelPtr->mLights)
		{
			if (l.dirType.w > 0.5f)
			{
				// Directional light; collect separately to add at the end
				dirLights.push_back(l);
			}
			else
			{
				// Point/spot light; add directly if we have space
				mStaticLights.push_back(l);
			}
		}
	}

	// Append directional lights at the end
	mStaticLights.insert(mStaticLights.end(), dirLights.begin(), dirLights.end());

	if (mStaticLights.size() < cMaxLights)
	{
		mSunIndex = static_cast<int>(mStaticLights.size()); // Capture index
		LightData sunLight{};
		sunLight.dirType = DirectX::XMFLOAT4(mSunDirection.x, mSunDirection.y, mSunDirection.z, 1.0f); // directional flag

		DirectX::XMFLOAT3 finalColor = mSunEnabled ? mSunColor : DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
		sunLight.diffuseColor = DirectX::XMFLOAT4(1.0f, 1.0f, 0.9f, 1.0f);
		sunLight.specularColor = DirectX::XMFLOAT4(1.0f, 1.0f, 0.9f, 1.0f);
		mStaticLights.push_back(sunLight);
	}

	UploadLightsToGPU();
}

void Renderer::UploadLightsToGPU()
{
	int totalLights = static_cast<int>(std::min<size_t>(mStaticLights.size(), cMaxLights));
	int pointLightCount = 0;
	for (int i = 0; i < totalLights; ++i)
	{
		// dirType.w: 0 = Point, 1 = Directional
		if (mStaticLights[i].dirType.w < 0.5f)
			pointLightCount++;
		else
			break;
	}

	memcpy(mConstantBufferData.lights, mStaticLights.data(), totalLights * sizeof(LightData));
	mConstantBufferData.numLights = totalLights;
	mConstantBufferData.numPointLights = pointLightCount;

	void* pData;
	D3D12_RANGE readRange = { 0, 0 }; // We do not intend to read from this resource on the CPU.

	HRESULT hr = mGlobalLightBuffer.Get()->Map(0, &readRange, &pData);
	ASSERT_HR(hr, "Failed to map constant buffer for light update.");

	memcpy(pData, mStaticLights.data(), mStaticLights.size() * sizeof(LightData));
	mGlobalLightBuffer.Get()->Unmap(0, nullptr);
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

	mWidth = width;
	mHeight = height;
	mRenderWidth = width;
	mRenderHeight = height;

	// setup timer for delta time
	LARGE_INTEGER freq;
	QueryPerformanceFrequency(&freq);
	mSecondsPerCount = 1.0 / static_cast<double>(freq.QuadPart);
	QueryPerformanceCounter(&mPrevCounter);

	// Shader-visible SRV heap for textures (increase capacity for many material descriptors)
	mSrvHeap.Initialize(mDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, Config::cNumberOfSrvDescriptors, true);
	mRtvHeap.Initialize(mDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 7, false);
	AllocateHandles();

	// Shared upload heap (512 MB)
	mUploadHeap.Initialize(mDevice.Get(), 512ull * 1024ull * 1024ull);
	mShaderCompiler.Initialize();

	InitializeTextureLoader();
	InitializeRootSignatures();
	InitializePipelineState();
	InitializeTonemapPipeline();
	InitializeTonemapResources();

	InitializeStreamline();
	InitializeDLSSRR();

	InitializeDepthBuffer();

	InitializeGBufferResources();
	InitializeCompositeResources();
	CreateLightBuffer();

	InitializeReflectionResources();

	InitializeDummyTextures();

	mRayTracingBuilder.Initialize(mDevice.Get(), mCommandList.Get(), &mCommandQueue);

	InitializeRayTracingPipelines();

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
	InputManager::Instance.RegisterKeyPressedCallback('G',
		[this]() {
			this->InitializePipelineState();
			this->InitializeRayTracingPipelines();
		});
	InputManager::Instance.RegisterKeyPressedCallback('H', std::bind(&Renderer::SetRenderMode, this, RenderMode::Hybrid));
	InputManager::Instance.RegisterKeyPressedCallback('P', std::bind(&Renderer::SetRenderMode, this, RenderMode::ForwardPhong));
	InputManager::Instance.RegisterKeyPressedCallback('R', std::bind(&Renderer::SetRenderMode, this, RenderMode::RayTraced));


	SetAllResourcesNames();
}

void Renderer::SetDLSSMode(sl::DLSSMode mode)
{
	//return;
	if (mDLSSMode == mode)
		return;

	mDLSSMode = mode;
	wcout << "DLSS Mode changed to: " << static_cast<int>(mode) << endl;

	// TODO: change after DLSS reinit fix
	OnResize(mWidth, mHeight);

	//if (mDLSSRREnabled)
	//{
	//	// Re-initialize DLSS with new mode
	//	slFreeResources(sl::kFeatureDLSS_RR, mSlViewport);
	//	mDLSSRREnabled = false;
	//}

	//if (mDLSSMode != sl::DLSSMode::eOff)
	//{
	//	InitializeDLSSRR();
	//}
}

Renderer::~Renderer()
{
	wcout << L"Renderer destructor: cleaning up GPU resources..." << endl;

	// Wait for all GPU operations to complete before destroying resources
	mCommandQueue.Flush();

	CleanupStreamline();

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

	// TODO: uncomment after DLSS reinit fix
	//if (width == mWidth && height == mHeight)
	//	return; // No actual resize

	wcout << "Resizing renderer to " << width << "x" << height << endl;

	// 1. Wait for GPU to complete all work
	mCommandQueue.Flush();

	// 2. Update dimensions
	mWidth = width;
	mHeight = height;

	// 2. Resize SwapChain
	mSwapChain.Resize(width, height);


	// =========================================================================
	// 3. Resize DLSS Resources
	// =========================================================================
	if (mDLSSRREnabled)
	{
		slFreeResources(sl::kFeatureDLSS_RR, mSlViewport);
		mDLSSRREnabled = false;
		InitializeDLSSRR();
	}
	else
	{
		mRenderWidth = mWidth;
		mRenderHeight = mHeight;
	}

	// 4. Resize Depth Buffer & Update Descriptors
	InitializeDepthBuffer();
	// 5. Resize G-Buffers
	InitializeGBufferResources();
	// 6. Resize Compute Output
	InitializeCompositeResources();
	// 7. Resize Reflection Targets
	InitializeReflectionResources();
	// 8. Resize Tonemap Resources
	InitializeTonemapResources();

	// 9. Update Viewport and Scissor Rect
	mViewport.Width = static_cast<FLOAT>(width);
	mViewport.Height = static_cast<FLOAT>(height);

	// 10. Update scissor rect
	mScissorRect.right = static_cast<LONG>(width);
	mScissorRect.bottom = static_cast<LONG>(height);

	std::cout << "Resize complete!" << endl;
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
			mMaskedSingleSidedMeshes.empty() && mMaskedDoubleSidedMeshes.empty() &&
			mTransparentSingleSidedMeshes.empty())
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
			mMaskedSingleSidedMeshes.empty() && mMaskedDoubleSidedMeshes.empty() &&
			mTransparentSingleSidedMeshes.empty())
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
		BuildRayTracingAccelerationStructures();
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
		mMaskedSingleSidedMeshes.size() + mMaskedDoubleSidedMeshes.size() +
		mTransparentSingleSidedMeshes.size();

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
	mMaskedSingleSidedMeshes.clear();
	mMaskedDoubleSidedMeshes.clear();
	mTransparentSingleSidedMeshes.clear();

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

	mSceneLoaded = false;
	mTLAS.Reset();
	mTLAS_Scratch.Reset();
	mInstanceDescBuffer.Reset();

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
		mMaskedSingleSidedMeshes,
		mMaskedDoubleSidedMeshes,
		mTransparentSingleSidedMeshes,
		mTransparentDoubleSidedMeshes);

	// 2. Allocate TLAS instance desc buffer
	UINT totalMeshes = static_cast<UINT>(
		mOpaqueSingleSidedMeshes.size() +
		mOpaqueDoubleSidedMeshes.size() +
		mMaskedSingleSidedMeshes.size() +
		mMaskedDoubleSidedMeshes.size() +
		mTransparentSingleSidedMeshes.size() +
		mTransparentDoubleSidedMeshes.size());

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
		mMaskedSingleSidedMeshes,
		mMaskedDoubleSidedMeshes,
		mTransparentSingleSidedMeshes,
		mTransparentDoubleSidedMeshes,
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



	// --- HYBRID RAY TRACING PIPELINE SETUP ---
	// 1. mComputeRootSignature can be reused as global root signature

	// 2. Local root signature
	// 2. Build Local Root Signature 
	CD3DX12_ROOT_PARAMETER1 localParams[4]{};

	// Param 0: Index buffer (t0)
	localParams[0].InitAsShaderResourceView(0, 1);

	// Param 1: Vertex buffer (t1)
	localParams[1].InitAsShaderResourceView(1, 1);

	// Param 2: Texture table (t2)
	CD3DX12_DESCRIPTOR_RANGE1 texRange{};
	texRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 2, 1); // t0-t4 space1
	localParams[2].InitAsDescriptorTable(1, &texRange);

	localParams[3].InitAsConstants(sizeof(MeshMaterialData) / 4, 0, 1);

	CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_ANISOTROPIC); // s0

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC localDesc{};
	localDesc.Init_1_1(4, localParams, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

	D3D12RootSignature localRootSig;
	localRootSig.Initialize(mDevice.Get(), localDesc);
	{
		//D3D12RootSignature globalRootSig;
		mRtGlobalRootSignature.InitializeHybridRTGlobalRS(mDevice.Get());

		HLSLShader libraryShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/DeferredRT.hlsl", L"lib_6_5", {}, L"");


		RTPipelineSettings reflSettings;
		reflSettings.maxPayloadSize = sizeof(float) * 8; // Color + Depth
		// 3. Initialize Pipeline
		mReflectionsPipeline.Initialize(mDevice.Get(), &mRtGlobalRootSignature, &localRootSig, libraryShader.GetShaderBlob(), reflSettings);
	}

	{
		// --- FULL RAY TRACING PIPELINE SETUP ---
		mFullRTGlobalRootSignature.InitializeFullRTGlobalRS(mDevice.Get());

		HLSLShader fullRtLibraryShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/FullRT.hlsl", L"lib_6_5", {}, L"");
		//HLSLShader fullRtLibraryShader = mShaderCompiler.LoadFromCso(L"../x64/Debug/FullRT.cso");

		RTPipelineSettings fullRtSettings;
		fullRtSettings.maxPayloadSize = sizeof(float) * 23; 
		mFullRTPipeline.Initialize(mDevice.Get(), &mFullRTGlobalRootSignature, &localRootSig, fullRtLibraryShader.GetShaderBlob(), fullRtSettings);
	}
	// 4. Build Shader Binding Table (SBT)
	std::initializer_list<std::span<const MeshGpuData>> allMeshes =
	{
		mOpaqueSingleSidedMeshes,
		mOpaqueDoubleSidedMeshes,
		mMaskedSingleSidedMeshes,
		mMaskedDoubleSidedMeshes,
		mTransparentSingleSidedMeshes,
		mTransparentDoubleSidedMeshes
	};
	mReflectionsPipeline.BuildSBT(mDevice.Get(), allMeshes);
	mFullRTPipeline.BuildSBT(mDevice.Get(), allMeshes);
}

void Renderer::SetAllResourcesNames()
{
	mConstantBuffer.Get()->SetName(L"Constant Buffer");
	mDepthBuffer.GetResource()->SetName(L"Depth Buffer");
	mGBufferAlbedo.Get()->SetName(L"G-Buffer Albedo");
	mGBufferNormal.Get()->SetName(L"G-Buffer Normal");
	mGBufferMaterial.Get()->SetName(L"G-Buffer Material");
	mGBufferEmission.Get()->SetName(L"G-Buffer Emissive");
	mGBufferVelocity.Get()->SetName(L"G-Buffer Velocity");
	mCompositeOutputTexture.Get()->SetName(L"Compute Output Texture");
	mTonemapOutputTexture.Get()->SetName(L"Tonemap Output Texture");
	mOutDiffuseTex.Get()->SetName(L"Reflection Out Diffuse Texture");
	mOutSpecularTex.Get()->SetName(L"Reflection Out Specular Texture");
	mDLSSOutputTexture.Get()->SetName(L"DLSS Output Texture");
	//	mTLAS.Get()->SetName(L"Top-Level Acceleration Structure");
	//	mTLAS_Scratch.Get()->SetName(L"TLAS Scratch Buffer");
	//	mInstanceDescBuffer.Get()->SetName(L"TLAS Instance Descriptions");
	mGlobalLightBuffer.Get()->SetName(L"Global Light Buffer");
	mOutAlbedoSpecularTex.Get()->SetName(L"Reflection Out AlbedoSpecular Texture");
	mOutAlbedoTex.Get()->SetName(L"Reflection Out Albedo Texture");

}

void Renderer::RenderHybrid(const Camera& camera)
{
	auto view = camera.GetViewMatrix();
	auto invView = DirectX::XMMatrixInverse(nullptr, view);
	auto proj = camera.GetProjMatrix();
	auto invProj = DirectX::XMMatrixInverse(nullptr, proj);
	auto viewProj = view * proj;
	auto invViewProj = DirectX::XMMatrixInverse(nullptr, viewProj);
	auto cameraPos = camera.GetPosition();
	auto cameraFwd = camera.GetForward();

	auto jitterMatrix = DirectX::XMMatrixTranslation(mJitter.x * 2.0f, -mJitter.y * 2.0f, 0.0f);
	auto jitteredProj = proj * jitterMatrix;
	auto jitteredViewProj = view * jitteredProj;

	size_t cbOffset;
	{
		mConstantBufferData.vpMatrix = jitteredViewProj;
		mConstantBufferData.InvVpMatrix = invViewProj;
		mConstantBufferData.viewPos = cameraPos;
		mConstantBufferData.frameCount = mFrameCount;

		mConstantBufferData.shadowsEnabled = mShadowsEnabled ? 1 : 0;
		mConstantBufferData.reflectionsEnabled = mReflectionsEnabled ? 1 : 0;
		mConstantBufferData.maxRecursionDepth = mMaxRecursionDepth;

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
			CD3DX12_RESOURCE_BARRIER::Transition(mGBufferVelocity.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET),
		};
		mCommandList.Get()->ResourceBarrier(_countof(barriers), barriers);

		// B. Clear Targets
		D3D12_CPU_DESCRIPTOR_HANDLE rtvHandles[] = {
			mRtvHeap.GetCpuHandle(0),
			mRtvHeap.GetCpuHandle(1),
			mRtvHeap.GetCpuHandle(2),
			mRtvHeap.GetCpuHandle(3),
			mRtvHeap.GetCpuHandle(4),
		};

		float clearColorBlack[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		float clearColorNormal[4] = { 0.5f, 0.5f, 1.0f, 1.0f };
		float clearColorMaterial[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		float clearColorVelocity[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
		float clearDepth = 1.0f;
		mCommandList.Get()->ClearRenderTargetView(rtvHandles[0], clearColorBlack, 0, nullptr);
		mCommandList.Get()->ClearRenderTargetView(rtvHandles[1], clearColorNormal, 0, nullptr);
		mCommandList.Get()->ClearRenderTargetView(rtvHandles[2], clearColorMaterial, 0, nullptr);
		mCommandList.Get()->ClearRenderTargetView(rtvHandles[3], clearColorBlack, 0, nullptr);
		mCommandList.Get()->ClearRenderTargetView(rtvHandles[4], clearColorVelocity, 0, nullptr);
		mCommandList.Get()->ClearDepthStencilView(mDepthBuffer.GetDSVHandle(), D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

		// C. Set Render Targets
		D3D12_VIEWPORT renderViewport = { 0.0f, 0.0f, static_cast<float>(mRenderWidth), static_cast<float>(mRenderHeight), 0.0f, 1.0f };
		D3D12_RECT renderScissorRect = { 0, 0, static_cast<LONG>(mRenderWidth), static_cast<LONG>(mRenderHeight) };
		auto dsvHandle = mDepthBuffer.GetDSVHandle();
		mCommandList.Get()->OMSetRenderTargets(_countof(rtvHandles), rtvHandles, FALSE, &dsvHandle);
		mCommandList.Get()->RSSetViewports(1, &renderViewport);
		mCommandList.Get()->RSSetScissorRects(1, &renderScissorRect);

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
		for (const auto& mesh : mMaskedSingleSidedMeshes)
			DrawMesh(mesh);
		mCommandList.Get()->SetPipelineState(mPipelineStateMaskedDouble.Get());
		for (const auto& mesh : mMaskedDoubleSidedMeshes)
			DrawMesh(mesh);
	}

	// =========================================================================
	// STAGE 2: REFLECTIONS (DXR Pipeline)
	// =========================================================================
	{
		// 1. Barrier: Output needs to be UAV
		D3D12_RESOURCE_BARRIER barriers[]
		{

			CD3DX12_RESOURCE_BARRIER::Transition(mGBufferAlbedo.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(mGBufferNormal.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(mGBufferMaterial.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(mDepthBuffer.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(mGBufferVelocity.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(mGBufferEmission.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),

			CD3DX12_RESOURCE_BARRIER::Transition(mOutDiffuseTex.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			CD3DX12_RESOURCE_BARRIER::Transition(mOutSpecularTex.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
		};
		mCommandList.Get()->ResourceBarrier(_countof(barriers), barriers);

		// 2. Dispatch Rays
		// Note: The Global Root Sig needs binding just like a Compute Shader
		mCommandList.Get()->SetComputeRootSignature(mRtGlobalRootSignature.Get());
		mCommandList.Get()->SetComputeRootConstantBufferView(0, mConstantBuffer.Get()->GetGPUVirtualAddress() + cbOffset);
		mCommandList.Get()->SetComputeRootDescriptorTable(1, mSrvHandle_GBufferAlbedo.gpuHandle); // G-Buffer
		mCommandList.Get()->SetComputeRootShaderResourceView(2, mTLAS.Get()->GetGPUVirtualAddress());
		mCommandList.Get()->SetComputeRootShaderResourceView(3, mGlobalLightBuffer.Get()->GetGPUVirtualAddress());

		// Bind Reflection Output UAV (Slot u0 in DeferredRT.hlsl)
		mCommandList.Get()->SetComputeRootDescriptorTable(4, mUavHandle_Diffuse.gpuHandle);

		mReflectionsPipeline.Dispatch(mCommandList.Get(), mRenderWidth, mRenderHeight);
	}
	// =========================================================================
	// STAGE 3: COMPOSITE PASS
	// =========================================================================
	{
		// Barriers: Reflection -> Read, DirectLight -> Read, Output -> Write
			// (For simplicity, let's say we write back into mComputeOutputTexture)
		D3D12_RESOURCE_BARRIER barriers[]
		{
			CD3DX12_RESOURCE_BARRIER::Transition(mOutDiffuseTex.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(mOutSpecularTex.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(mCompositeOutputTexture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
		};
		mCommandList.Get()->ResourceBarrier(_countof(barriers), barriers);

		// Bind Composite Pipeline
		mCommandList.Get()->SetComputeRootSignature(mCompositeRootSignature.Get());
		mCommandList.Get()->SetPipelineState(mPipelineStateComposite.Get());

		// Bind Descriptors
		// Slot 0: CBV
		mCommandList.Get()->SetComputeRootConstantBufferView(0, mConstantBuffer.Get()->GetGPUVirtualAddress() + cbOffset);

		// Slot 1: Diffuse, Specular, Albedo, SpecularAlbedo SRVs (t0 - t3)
		mCommandList.Get()->SetComputeRootDescriptorTable(1, mSrvHandle_Diffuse.gpuHandle);

		// Slot 2: Emissive
		mCommandList.Get()->SetComputeRootDescriptorTable(2, mSrvHandle_GBufferEmissive.gpuHandle);

		// Slot 3: Output UAV (u0)
		mCommandList.Get()->SetComputeRootDescriptorTable(3, mUavHandle_CompositeOutput.gpuHandle);

		// Dispatch
		mCommandList.Get()->Dispatch((mRenderWidth + 7) / 8, (mRenderHeight + 7) / 8, 1);

		D3D12_RESOURCE_BARRIER cleanup[]
		{
			CD3DX12_RESOURCE_BARRIER::Transition(mOutDiffuseTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
			CD3DX12_RESOURCE_BARRIER::Transition(mOutSpecularTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
		};

		mCommandList.Get()->ResourceBarrier(_countof(cleanup), cleanup);
	}

	// =========================================================================
	// STAGE 4:DLSS RAY RECONSTRUCTION
	// =========================================================================
	{
		if (mDLSSRREnabled)
		{
			D3D12_RESOURCE_BARRIER barriers[]
			{
				CD3DX12_RESOURCE_BARRIER::Transition(mOutDiffuseTex.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mOutSpecularTex.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mDLSSOutputTexture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
				//CD3DX12_RESOURCE_BARRIER::Transition(mGBufferVelocity.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mCompositeOutputTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			};
			mCommandList.Get()->ResourceBarrier(_countof(barriers), barriers);
			float nearZ = camera.GetNearZ();
			float farZ = camera.GetFarZ();
			float fovY = camera.GetFovY();
			float aspectRatio = camera.GetAspect();

			EvaluateDLSSRR(view, proj, invView, invProj, cameraPos, cameraFwd, nearZ, farZ, fovY, aspectRatio);

			D3D12_RESOURCE_BARRIER restoreBarriers[]
			{
				//					CD3DX12_RESOURCE_BARRIER::Transition(mComputeOutputTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET),
				//					CD3DX12_RESOURCE_BARRIER::Transition(mDLSSOutputTexture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
									// Transitions for next frame (inputs back to common/UAV if needed? usually handled at start of frame)
				CD3DX12_RESOURCE_BARRIER::Transition(mOutDiffuseTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
				CD3DX12_RESOURCE_BARRIER::Transition(mOutSpecularTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
				CD3DX12_RESOURCE_BARRIER::Transition(mOutAlbedoTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
				CD3DX12_RESOURCE_BARRIER::Transition(mOutAlbedoSpecularTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
				CD3DX12_RESOURCE_BARRIER::Transition(mCompositeOutputTexture.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),

			};
			mCommandList.Get()->ResourceBarrier(_countof(restoreBarriers), restoreBarriers);
		}
		else
		{
			// not implemented
			throw std::logic_error("Not implemented");
		}
	}

	// =========================================================================================
	// STAGE 5: TRANSPARENT FORWARD PASS
	// =========================================================================================
	{
		D3D12_RESOURCE_BARRIER barriers[]
		{
			// 1. Transition Output Texture: UAV (from Compute) -> RENDER_TARGET
			//CD3DX12_RESOURCE_BARRIER::Transition(mComputeOutputTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_RENDER_TARGET),
			// 2. Transition Depth: SHADER_RESOURCE (from Compute) -> DEPTH_READ
			// We need to READ depth to occlude glass behind walls, but we don't need to WRITE (usually).
			CD3DX12_RESOURCE_BARRIER::Transition(mDepthBuffer.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_DEPTH_READ),
		};
		mCommandList.Get()->ResourceBarrier(_countof(barriers), barriers);

		// Bind Targets
		// We write Color to ComputeOutput, and Read Depth from DepthBuffer
		//D3D12_CPU_DESCRIPTOR_HANDLE rtv = mGBufferRtvHeap.GetCpuHandle(mRtvIndex_ComputeOutput);
		//D3D12_CPU_DESCRIPTOR_HANDLE dsv = mDepthBuffer.GetDSVHandle();

		//mCommandList.Get()->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
		//mCommandList.Get()->RSSetViewports(1, &mViewport);
		//mCommandList.Get()->RSSetScissorRects(1, &mScissorRect);

		//// Draw Transparent Meshes
		//mCommandList.Get()->SetGraphicsRootSignature(mMeshRootSignature.Get());
		//mCommandList.Get()->SetPipelineState(mPipelineStateTransparentSingle.Get());
		//mCommandList.Get()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		//mCommandList.Get()->SetGraphicsRootConstantBufferView(0, mConstantBuffer.Get()->GetGPUVirtualAddress());

		//for (const auto& mesh : mTransparentSingleSidedMeshes)
		//	DrawMesh(mesh);

		//for (const auto& mesh : mTransparentDoubleSidedMeshes)
		//	DrawMesh(mesh);
	}
}


void Renderer::RenderPhong(const Camera& camera)
{
	auto viewProj = camera.GetViewProjection();
	auto cameraPos = camera.GetPosition();

	size_t cbOffset;
	{
		mConstantBufferData.vpMatrix = viewProj;
		mConstantBufferData.viewPos = cameraPos;
		mConstantBufferData.frameCount = mFrameCount;

		UINT currentBackBufferIndex = mSwapChain.GetCurrentBackBufferIndex();
		size_t alignedSize = (sizeof(ConstantBufferData) + 255) & ~255; // Align to 256 bytes
		cbOffset = alignedSize * currentBackBufferIndex;

		void* pData;
		mConstantBuffer.Get()->Map(0, nullptr, &pData);
		uint8_t* pByteData = reinterpret_cast<uint8_t*>(pData);
		memcpy(pByteData + cbOffset, &mConstantBufferData, sizeof(ConstantBufferData));
		mConstantBuffer.Get()->Unmap(0, nullptr);
		SortTransparentMeshes(cameraPos);
	}

	mCommandQueue.WaitForFenceInFrame(mSwapChain.GetCurrentBackBufferIndex());
	mCommandList.ResetCommandList(mSwapChain.GetCurrentBackBufferIndex());

	ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
	mCommandList.Get()->SetDescriptorHeaps(_countof(heaps), heaps);

	D3D12_RESOURCE_BARRIER barriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(mPhongOutputTexture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET),
		CD3DX12_RESOURCE_BARRIER::Transition(mNativeDepthBuffer.GetResource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_WRITE)
	};
	mCommandList.Get()->ResourceBarrier(_countof(barriers), barriers);

	auto rtvHandle = mRtvHandle_PhongOutput.cpuHandle;
	auto dsvHandle = mNativeDepthBuffer.GetDSVHandle();
	// Clear
	float clearColor[4] = { 0.0f, 0.0f, 0.0f, 1.0f }; // Black background
	mCommandList.Get()->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
	mCommandList.Get()->ClearDepthStencilView(dsvHandle, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);

	mCommandList.Get()->OMSetRenderTargets(1, &rtvHandle, FALSE, &dsvHandle);

	// Viewport
	mCommandList.Get()->RSSetViewports(1, &mViewport);
	mCommandList.Get()->RSSetScissorRects(1, &mScissorRect);

	// 3. Draw Geometry
	mCommandList.Get()->SetGraphicsRootSignature(mMeshRootSignature.Get());
	mCommandList.Get()->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mCommandList.Get()->SetGraphicsRootConstantBufferView(0, mConstantBuffer.Get()->GetGPUVirtualAddress() + cbOffset);

	// Opaque
	mCommandList.Get()->SetPipelineState(mPipelineStatePhongOpaqueSingle.Get());
	for (const auto& mesh : mOpaqueSingleSidedMeshes) DrawMesh(mesh);

	mCommandList.Get()->SetPipelineState(mPipelineStatePhongOpaqueDouble.Get());
	for (const auto& mesh : mOpaqueDoubleSidedMeshes) DrawMesh(mesh);

	// Masked
	mCommandList.Get()->SetPipelineState(mPipelineStatePhongMaskedSingle.Get());
	for (const auto& mesh : mMaskedSingleSidedMeshes) DrawMesh(mesh);

	mCommandList.Get()->SetPipelineState(mPipelineStatePhongMaskedDouble.Get());
	for (const auto& mesh : mMaskedDoubleSidedMeshes) DrawMesh(mesh);

	// Transparent
	mCommandList.Get()->SetPipelineState(mPipelineStatePhongTransparentSingle.Get());
	for (const auto& mesh : mTransparentSingleSidedMeshes) DrawMesh(mesh);

	mCommandList.Get()->SetPipelineState(mPipelineStatePhongTransparentDouble.Get());
	for (const auto& mesh : mTransparentDoubleSidedMeshes) DrawMesh(mesh);

	D3D12_RESOURCE_BARRIER endBarriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(mPhongOutputTexture.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
	};
	mCommandList.Get()->ResourceBarrier(_countof(endBarriers), endBarriers);
}

void Renderer::RenderFullRayTraced(const Camera& camera)
{
	auto view = camera.GetViewMatrix();
	auto invView = DirectX::XMMatrixInverse(nullptr, view);
	auto proj = camera.GetProjMatrix();
	auto invProj = DirectX::XMMatrixInverse(nullptr, proj);
	auto viewProj = view * proj;
	auto invViewProj = DirectX::XMMatrixInverse(nullptr, viewProj);
	auto cameraPos = camera.GetPosition();
	auto cameraFwd = camera.GetForward();

	auto jitterMatrix = DirectX::XMMatrixTranslation(mJitter.x * 2.0f, -mJitter.y * 2.0f, 0.0f);
	auto jitteredProj = proj * jitterMatrix;
	auto jitteredViewProj = view * jitteredProj;

	size_t cbOffset;
	{
		mConstantBufferData.vpMatrix = jitteredViewProj;
		mConstantBufferData.InvVpMatrix = invViewProj;
		mConstantBufferData.viewPos = cameraPos;
		mConstantBufferData.frameCount = mFrameCount;

		mConstantBufferData.shadowsEnabled = mShadowsEnabled ? 1 : 0;
		mConstantBufferData.reflectionsEnabled = mReflectionsEnabled ? 1 : 0;
		mConstantBufferData.maxRecursionDepth = mMaxRecursionDepth;

		UINT currentBackBufferIndex = mSwapChain.GetCurrentBackBufferIndex();
		size_t alignedSize = (sizeof(ConstantBufferData) + 255) & ~255; // Align to 256 bytes
		cbOffset = alignedSize * currentBackBufferIndex;

		void* pData;
		mConstantBuffer.Get()->Map(0, nullptr, &pData);
		uint8_t* pByteData = reinterpret_cast<uint8_t*>(pData);
		memcpy(pByteData + cbOffset, &mConstantBufferData, sizeof(ConstantBufferData));
		mConstantBuffer.Get()->Unmap(0, nullptr);

		// Sort transparent meshes back-to-front each frame (temporary solution)
		//SortTransparentMeshes(cameraPos);
	}

	// Wait for GPU to finish with the current back buffer
	mCommandQueue.WaitForFenceInFrame(mSwapChain.GetCurrentBackBufferIndex());
	// Open command list
	mCommandList.ResetCommandList(mSwapChain.GetCurrentBackBufferIndex());

	// Bind descriptor heap
	ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
	mCommandList.Get()->SetDescriptorHeaps(_countof(heaps), heaps);

	D3D12_RESOURCE_BARRIER barriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(mOutDiffuseTex.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		CD3DX12_RESOURCE_BARRIER::Transition(mOutSpecularTex.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		CD3DX12_RESOURCE_BARRIER::Transition(mOutAlbedoTex.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		CD3DX12_RESOURCE_BARRIER::Transition(mOutAlbedoSpecularTex.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
		CD3DX12_RESOURCE_BARRIER::Transition(mCompositeOutputTexture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),

		CD3DX12_RESOURCE_BARRIER::Transition(mGBufferNormal.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
	CD3DX12_RESOURCE_BARRIER::Transition(mGBufferEmission.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
	CD3DX12_RESOURCE_BARRIER::Transition(mGBufferMaterial.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
	CD3DX12_RESOURCE_BARRIER::Transition(mRTDepthTexture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS)
	};
	mCommandList.Get()->ResourceBarrier(_countof(barriers), barriers);

	// 2. Set State
	mCommandList.Get()->SetComputeRootSignature(mFullRTGlobalRootSignature.Get());
	mCommandList.Get()->SetComputeRootConstantBufferView(0, mConstantBuffer.Get()->GetGPUVirtualAddress() + cbOffset);

	// 3. Bind Parameters
	// Param 1: Split UAVs (u0-u3) -> These are contiguous in your heap (Diffuse/Spec/Albedo/AlbedoSpec)
	mCommandList.Get()->SetComputeRootDescriptorTable(1, mUavHandle_Diffuse.gpuHandle);

	// Param 2: Final UAV (u4) -> The Composite Output Handle
	mCommandList.Get()->SetComputeRootDescriptorTable(2, mUavHandle_CompositeOutput.gpuHandle);

	// Param 5: G-Buffer UAVs (u5-u7) [NEW]
	mCommandList.Get()->SetComputeRootDescriptorTable(3, mUavHandle_GBufferNormal.gpuHandle);

	// Param 3: TLAS (t5)
	mCommandList.Get()->SetComputeRootShaderResourceView(4, mTLAS.Get()->GetGPUVirtualAddress());

	// Param 4: Lights (t6)
	mCommandList.Get()->SetComputeRootShaderResourceView(5, mGlobalLightBuffer.Get()->GetGPUVirtualAddress());

	// 4. Dispatch
	mFullRTPipeline.Dispatch(mCommandList.Get(), mRenderWidth, mRenderHeight);

	if (mDLSSRREnabled)
	{
		D3D12_RESOURCE_BARRIER barriers[]
		{
			CD3DX12_RESOURCE_BARRIER::Transition(mOutDiffuseTex.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(mOutSpecularTex.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(mOutAlbedoTex.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(mOutAlbedoSpecularTex.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(mCompositeOutputTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),

			CD3DX12_RESOURCE_BARRIER::Transition(mDLSSOutputTexture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),

			CD3DX12_RESOURCE_BARRIER::Transition(mDepthBuffer.GetResource(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(mGBufferVelocity.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(mGBufferNormal.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(mGBufferMaterial.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(mGBufferEmission.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			CD3DX12_RESOURCE_BARRIER::Transition(mRTDepthTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE)
		};
		mCommandList.Get()->ResourceBarrier(_countof(barriers), barriers);
		float nearZ = camera.GetNearZ();
		float farZ = camera.GetFarZ();
		float fovY = camera.GetFovY();
		float aspectRatio = camera.GetAspect();

		EvaluateDLSSRR(view, proj, invView, invProj, cameraPos, cameraFwd, nearZ, farZ, fovY, aspectRatio);

		D3D12_RESOURCE_BARRIER restoreBarriers[]
		{
			//					CD3DX12_RESOURCE_BARRIER::Transition(mComputeOutputTexture.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_RENDER_TARGET),
			//					CD3DX12_RESOURCE_BARRIER::Transition(mDLSSOutputTexture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
								// Transitions for next frame (inputs back to common/UAV if needed? usually handled at start of frame)
			CD3DX12_RESOURCE_BARRIER::Transition(mOutDiffuseTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
			CD3DX12_RESOURCE_BARRIER::Transition(mOutSpecularTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
			CD3DX12_RESOURCE_BARRIER::Transition(mOutAlbedoTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
			CD3DX12_RESOURCE_BARRIER::Transition(mOutAlbedoSpecularTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),

			CD3DX12_RESOURCE_BARRIER::Transition(mCompositeOutputTexture.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),

			CD3DX12_RESOURCE_BARRIER::Transition(mDepthBuffer.GetResource(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
			CD3DX12_RESOURCE_BARRIER::Transition(mGBufferVelocity.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
			CD3DX12_RESOURCE_BARRIER::Transition(mGBufferNormal.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
			CD3DX12_RESOURCE_BARRIER::Transition(mGBufferMaterial.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
			CD3DX12_RESOURCE_BARRIER::Transition(mGBufferEmission.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
			CD3DX12_RESOURCE_BARRIER::Transition(mRTDepthTexture.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON)
		};
		mCommandList.Get()->ResourceBarrier(_countof(restoreBarriers), restoreBarriers);
	}
	else
	{
		// not implemented
		throw std::logic_error("Not implemented");
	}
}

void Renderer::InitializeGBufferResources()
{
	// 1. Initialize Heaps
	// Create RTV Heap (Capacity 5, Not Visible)
	//mGBufferRtvHeap.Initialize(mDevice.Get(), D3D12_DESCRIPTOR_HEAP_TYPE_RTV, 6, false);

	// 2. Define Resource Descriptors (Standard D3DX12 code...)
	auto albedoDescc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R8G8B8A8_UNORM,
		mRenderWidth,
		mRenderHeight,
		1, // array size
		1, // mip levels
		1, // sample count
		0, // sample quality
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
	);
	auto normalDescc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		mRenderWidth,
		mRenderHeight,
		1, // array size
		1, // mip levels
		1, // sample count
		0, // sample quality
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
	);
	auto materialDescc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R32G32_FLOAT,
		mRenderWidth,
		mRenderHeight,
		1, // array size
		1, // mip levels
		1, // sample count
		0, // sample quality
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
	);
	auto emissiveDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		mRenderWidth,
		mRenderHeight,
		1, // array size
		1, // mip levels
		1, // sample count
		0, // sample quality
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET | D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
	);
	auto velocityDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R16G16_FLOAT,
		mRenderWidth,
		mRenderHeight,
		1, // array size
		1, // mip levels
		1, // sample count
		0, // sample quality
		D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
	);

	auto depthDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R32_FLOAT,
		mRenderWidth, mRenderHeight,
		1, 1, 1, 0,
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
	);



	// Clear Values...
	D3D12_CLEAR_VALUE clearBlack = { DXGI_FORMAT_R8G8B8A8_UNORM, { 0.0f, 0.0f, 0.0f, 1.0f } };
	D3D12_CLEAR_VALUE clearNormal = { DXGI_FORMAT_R16G16B16A16_FLOAT, { 0.5f, 0.5f, 1.0f, 1.0f } };
	D3D12_CLEAR_VALUE clearMaterial = { DXGI_FORMAT_R32G32_FLOAT, { 0.0f, 0.0f, 0.0f, 1.0f } };
	D3D12_CLEAR_VALUE clearEmissive = { DXGI_FORMAT_R16G16B16A16_FLOAT, { 0.0f, 0.0f, 0.0f, 1.0f } };
	D3D12_CLEAR_VALUE clearVelocity = { DXGI_FORMAT_R16G16_FLOAT, { 0.0f, 0.0f, 0.0f, 1.0f } };

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
	mGBufferVelocity.Initialize(
		mDevice.Get(),
		velocityDesc,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_STATE_COMMON,
		&clearVelocity);

	mRTDepthTexture.Initialize(
		mDevice.Get(),
		depthDesc,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_STATE_COMMON
	);

	// 3. Create RTVs using the Wrapper
	// Allocate 3 slots
	//mRtvHandle_GBufferAlbedo = mGBufferRtvHeap.Allocate();
	//mRtvHandle_GBufferNormal = mGBufferRtvHeap.Allocate();
	//mRtvHandle_GBufferMaterial = mGBufferRtvHeap.Allocate();
	//mRtvHandle_GBufferEmissive = mGBufferRtvHeap.Allocate();
	//mRtvHandle_GBufferVelocity = mGBufferRtvHeap.Allocate();

	// Create Views into the allocated handles
	mDevice.Get()->CreateRenderTargetView(mGBufferAlbedo.Get(), nullptr, mRtvHandle_GBufferAlbedo.cpuHandle);
	mDevice.Get()->CreateRenderTargetView(mGBufferNormal.Get(), nullptr, mRtvHandle_GBufferNormal.cpuHandle);
	mDevice.Get()->CreateRenderTargetView(mGBufferMaterial.Get(), nullptr, mRtvHandle_GBufferMaterial.cpuHandle);
	mDevice.Get()->CreateRenderTargetView(mGBufferEmission.Get(), nullptr, mRtvHandle_GBufferEmissive.cpuHandle);
	mDevice.Get()->CreateRenderTargetView(mGBufferVelocity.Get(), nullptr, mRtvHandle_GBufferVelocity.cpuHandle);

	// 4. Create SRVs (Inputs for Compute)
	//mSrvHandle_GBufferAlbedo = mSrvHeap.Allocate();
	//mSrvHandle_GBufferNormal = mSrvHeap.Allocate();
	//mSrvHandle_GBufferMaterial = mSrvHeap.Allocate();
	//mSrvHandle_Depth = mSrvHeap.Allocate();
	//mSrvHandle_GBufferEmissive = mSrvHeap.Allocate();
	//mSrvHandle_GBufferVelocity = mSrvHeap.Allocate();

	CreateTextureView(mGBufferAlbedo.Get(), DXGI_FORMAT_R8G8B8A8_UNORM, mSrvHandle_GBufferAlbedo.cpuHandle, 1);
	CreateTextureView(mGBufferNormal.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, mSrvHandle_GBufferNormal.cpuHandle, 1);
	CreateTextureView(mGBufferMaterial.Get(), DXGI_FORMAT_R32G32_FLOAT, mSrvHandle_GBufferMaterial.cpuHandle, 1);
	CreateTextureView(mGBufferEmission.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, mSrvHandle_GBufferEmissive.cpuHandle, 1);
	CreateTextureView(mGBufferVelocity.Get(), DXGI_FORMAT_R16G16_FLOAT, mSrvHandle_GBufferVelocity.cpuHandle, 1);


	// 5. Create UAVs (Outputs for Ray Tracing)
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Texture2D.MipSlice = 0;

	// Normal
	uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	mDevice.Get()->CreateUnorderedAccessView(
		mGBufferNormal.Get(),
		nullptr,
		&uavDesc,
		mUavHandle_GBufferNormal.cpuHandle
	);

	// Material	
	uavDesc.Format = DXGI_FORMAT_R32G32_FLOAT;
	mDevice.Get()->CreateUnorderedAccessView(
		mGBufferMaterial.Get(),
		nullptr,
		&uavDesc,
		mUavHandle_GBufferMaterial.cpuHandle
	);

	// Emissive
	uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	mDevice.Get()->CreateUnorderedAccessView(
		mGBufferEmission.Get(),
		nullptr,
		&uavDesc,
		mUavHandle_GBufferEmissive.cpuHandle
	);

	// RT Depth
	uavDesc.Format = DXGI_FORMAT_R32_FLOAT;
	mDevice.Get()->CreateUnorderedAccessView(
		mRTDepthTexture.Get(),
		nullptr,
		&uavDesc,
		mUavHandle_RTDepth.cpuHandle
	);
}

void Renderer::InitializeCompositeResources()
{
	// ====================================================================================
	// 1. Create Final Output Texture (UAV)
	// This is where the Composite Shader writes the merged result.
	// ====================================================================================
	{
		auto uavDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			mRenderWidth,
			mRenderHeight,
			1, // array size
			1, // mip levels
			1, // sample count
			0, // sample quality
			D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
		);

		mCompositeOutputTexture.Initialize(
			mDevice.Get(),
			uavDesc,
			D3D12_HEAP_TYPE_DEFAULT,
			D3D12_RESOURCE_STATE_COMMON);

		D3D12_UNORDERED_ACCESS_VIEW_DESC uavViewDesc = {};
		uavViewDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		//uavViewDesc.Format = DXGI_FORMAT_UNKNOWN;
		uavViewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
		mDevice.Get()->CreateUnorderedAccessView(
			mCompositeOutputTexture.Get(),
			nullptr,
			&uavViewDesc,
			mUavHandle_CompositeOutput.cpuHandle);

		mDevice.Get()->CreateRenderTargetView(
			mCompositeOutputTexture.Get(),
			nullptr,
			mRtvHandle_CompositeOutput.cpuHandle);
	}
	// ====================================================================================
	// 2. Create SRV for reading in DLSS / Tonemap Pass
	// ====================================================================================
	{
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		mDevice.Get()->CreateShaderResourceView(mCompositeOutputTexture.Get(), &srvDesc, mSrvHandle_CompositeOutput.cpuHandle);
	}

	// ====================================================================================
	// 3. Phong Output Texture
	// ====================================================================================
	{
		auto phongDesc = CD3DX12_RESOURCE_DESC::Tex2D(
			DXGI_FORMAT_R16G16B16A16_FLOAT,
			mWidth,
			mHeight,
			1, // array size
			1, // mip levels
			1, // sample count
			0, // sample quality
			D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
		);
		D3D12_CLEAR_VALUE clearVal = {};
		clearVal.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		clearVal.Color[0] = 0.0f;
		clearVal.Color[1] = 0.0f;
		clearVal.Color[2] = 0.0f;
		clearVal.Color[3] = 1.0f;

		// Create Resource
		mPhongOutputTexture.Initialize(
			mDevice.Get(),
			phongDesc,
			D3D12_HEAP_TYPE_DEFAULT,
			D3D12_RESOURCE_STATE_COMMON,
			&clearVal);

		// Create RTV
		mDevice.Get()->CreateRenderTargetView(mPhongOutputTexture.Get(), nullptr, mRtvHandle_PhongOutput.cpuHandle);

		// Create SRV
		D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
		srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
		srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
		srvDesc.Texture2D.MipLevels = 1;
		srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
		mDevice.Get()->CreateShaderResourceView(mPhongOutputTexture.Get(), &srvDesc, mSrvHandle_PhongOutput.cpuHandle);

		mPhongOutputTexture.Get()->SetName(L"Phong Output Texture");
	}
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
		mSrvHandle_LightBuffer.cpuHandle);
}

void Renderer::InitializeReflectionResources()
{
	// 1. Create Reflection Texture (Same size as screen, RGBA16F for HDR)
	auto desc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R16G16B16A16_FLOAT, // High precision for reflections
		mRenderWidth, mRenderHeight, 1, 1, 1, 0,
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
	mOutAlbedoTex.Initialize(
		mDevice.Get(),
		desc,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_STATE_COMMON);
	mOutAlbedoSpecularTex.Initialize(
		mDevice.Get(),
		desc,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_STATE_COMMON);

	mOutDiffuseTex.Get()->SetName(L"Out Diffuse Texture");
	mOutSpecularTex.Get()->SetName(L"Out Specular Texture");
	mOutAlbedoTex.Get()->SetName(L"Albedo Texture");
	mOutAlbedoSpecularTex.Get()->SetName(L"Albedo Specular Texture");

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	mDevice.Get()->CreateUnorderedAccessView(mOutDiffuseTex.Get(), nullptr, &uavDesc, mUavHandle_Diffuse.cpuHandle);
	mDevice.Get()->CreateUnorderedAccessView(mOutSpecularTex.Get(), nullptr, &uavDesc, mUavHandle_Specular.cpuHandle);
	mDevice.Get()->CreateUnorderedAccessView(mOutAlbedoTex.Get(), nullptr, &uavDesc, mUavHandle_Albedo.cpuHandle);
	mDevice.Get()->CreateUnorderedAccessView(mOutAlbedoSpecularTex.Get(), nullptr, &uavDesc, mUavHandle_AlbedoSpecular.cpuHandle);

	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MipLevels = 1;
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	mDevice.Get()->CreateShaderResourceView(mOutDiffuseTex.Get(), &srvDesc, mSrvHandle_Diffuse.cpuHandle);
	mDevice.Get()->CreateShaderResourceView(mOutSpecularTex.Get(), &srvDesc, mSrvHandle_Specular.cpuHandle);
	mDevice.Get()->CreateShaderResourceView(mOutAlbedoTex.Get(), &srvDesc, mSrvHandle_Albedo.cpuHandle);
	mDevice.Get()->CreateShaderResourceView(mOutAlbedoSpecularTex.Get(), &srvDesc, mSrvHandle_AlbedoSpecular.cpuHandle);
}


void Renderer::InitializeStreamline()
{
	sl::Preferences pref{};
	pref.showConsole = false;
	pref.logLevel = sl::LogLevel::eDefault;
	//pref.logLevel = sl::LogLevel::eOff;
	pref.pathsToPlugins = nullptr;	// Use default plugin path (next to executable)
	pref.numPathsToPlugins = 0;
	pref.applicationId = 231313132;

	sl::Feature featuresToLoad[] = { sl::kFeatureDLSS_RR };
	pref.featuresToLoad = featuresToLoad;
	pref.numFeaturesToLoad = _countof(featuresToLoad);
	pref.flags |= sl::PreferenceFlags::eUseFrameBasedResourceTagging;

	if (SL_FAILED(res, slInit(pref)))
	{
		std::cerr << "Failed to initialize Streamline. Result was: " << static_cast<int>(res) << std::endl;
		return;
	}

	if (SL_FAILED(res, slSetD3DDevice(mDevice.Get())))
	{
		std::cerr << "Failed to set D3D device for Streamline. Result was: " << static_cast<int>(res) << std::endl;
		return;
	}

	mStreamlineInitialized = true;
	std::cout << "Streamline initialized successfully." << std::endl;
}

void Renderer::SetSunColor(float r, float g, float b)
{
	mSunColor = { r, g, b };

	if (mSunIndex >= 0)
	{
		mStaticLights[mSunIndex].diffuseColor = { r, g, b, 1.0f };
		mStaticLights[mSunIndex].specularColor = { r, g, b, 1.0f };
		UploadLightsToGPU();
	}
}

void Renderer::SetSunEnabled(bool enabled)
{
	mSunEnabled = enabled;
	if (mSunIndex >= 0)
	{
		DirectX::XMFLOAT3 c = mSunEnabled ? mSunColor : DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);

		mStaticLights[mSunIndex].diffuseColor = { c.x, c.y, c.z, 1.0f };
		mStaticLights[mSunIndex].specularColor = { c.x, c.y, c.z, 1.0f };
		UploadLightsToGPU();
	}
}

void Renderer::SetSunDirection(float x, float y, float z)
{
	DirectX::XMVECTOR vDir = DirectX::XMVectorSet(x, y, z, 0.0f);
	vDir = DirectX::XMVector3Normalize(vDir);
	DirectX::XMStoreFloat3(&mSunDirection, vDir);

	if (mSunIndex >= 0)
	{
		LightData& sun = mStaticLights[mSunIndex];
		if (sun.dirType.w > 0.5f) // Directional light
			sun.dirType = { x, y, z, 1.0f };

		UploadLightsToGPU();
	}
}

void Renderer::InitializeDLSSRR()
{
	sl::DLSSDOptions options = {};
	options.mode = mDLSSMode;
	options.outputWidth = mWidth;
	options.outputHeight = mHeight;
	options.colorBuffersHDR = sl::Boolean::eTrue;
	options.preExposure = 1.0f;
	options.exposureScale = 1.0f;
	//options.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::ePacked;
	options.normalRoughnessMode = sl::DLSSDNormalRoughnessMode::eUnpacked;

	sl::DLSSDOptimalSettings optimalSettings{};

	if (SL_FAILED(res, slDLSSDGetOptimalSettings(options, optimalSettings)))
	{
		std::cerr << "Failed to get DLSS optimal settings. Result was: " << static_cast<int>(res) << std::endl;
		return;
	}

	mRenderWidth = optimalSettings.optimalRenderWidth;
	mRenderHeight = optimalSettings.optimalRenderHeight;

	std::cout << "DLSS Optimal Render Size: " << mRenderWidth << "x" << mRenderHeight << std::endl;

	auto dlssOutputDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R16G16B16A16_FLOAT,
		mWidth,
		mHeight,
		1, // array size
		1, // mip levels
		1, // sample count
		0, // sample quality
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET
	);

	mDLSSOutputTexture.Initialize(
		mDevice.Get(),
		dlssOutputDesc,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_STATE_COMMON);

	//mUavHandle_DlssOutput = mSrvHeap.Allocate();

	D3D12_UNORDERED_ACCESS_VIEW_DESC dlssUavViewDesc = {};
	dlssUavViewDesc.Format = DXGI_FORMAT_R16G16B16A16_FLOAT;
	dlssUavViewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	mDevice.Get()->CreateUnorderedAccessView(
		mDLSSOutputTexture.Get(),
		nullptr,
		&dlssUavViewDesc,
		mUavHandle_DlssOutput.cpuHandle);

	//mSrvHandle_DlssOutput = mSrvHeap.Allocate();
	CreateTextureView(mDLSSOutputTexture.Get(), DXGI_FORMAT_R16G16B16A16_FLOAT, mSrvHandle_DlssOutput.cpuHandle, 1);

	if (SL_FAILED(res, slDLSSDSetOptions(mSlViewport, options)))
	{
		std::cerr << "Failed to set DLSS options. Result was: " << static_cast<int>(res) << std::endl;
		return;
	}

	mDLSSRREnabled = true;
	std::cout << "DLSS initialized successfully." << std::endl;
}

void Renderer::UpdateCameraJitter()
{
	// Halton sequence for jitter (commonly used for TAA/DLSS)
	// Simple 2,3 Halton sequence implementation
	auto halton = [](int index, int base) {
		float result = 0.0f;
		float f = 1.0f / base;
		int i = index;
		while (i > 0)
		{
			result += f * (i % base);
			i /= base;
			f /= base;
		}
		return result;
		};

	int jitterIndex = mFrameCount % 16 + 1; // Use 16-sample jitter pattern
	float jitterX = halton(jitterIndex, 2) - 0.5f;
	float jitterY = halton(jitterIndex, 3) - 0.5f;

	mJitter.x = jitterX / static_cast<float>(mRenderWidth);
	mJitter.y = jitterY / static_cast<float>(mRenderHeight);
}

void Renderer::EvaluateDLSSRR(const DirectX::XMMATRIX& view,
	const DirectX::XMMATRIX& proj,
	const DirectX::XMMATRIX& invView,
	const DirectX::XMMATRIX& invProj,
	const DirectX::XMFLOAT3& cameraPos,
	const DirectX::XMFLOAT3& cameraForward,
	float nearZ, float farZ, float fovY, float aspectRatio)
{
	sl::FrameToken* frameToken = nullptr;
	if (SL_FAILED(res, slGetNewFrameToken(frameToken, &mFrameCount)))
	{
		std::cerr << "Failed to get new DLSS frame token. Result was: " << static_cast<int>(res) << std::endl;
		return;
	}

	sl::Constants slConstants{};
	slConstants.cameraViewToClip = XMMatrixToSLFloat4x4(proj);
	slConstants.clipToCameraView = XMMatrixToSLFloat4x4(invProj);


	// Clip to previous clip
	DirectX::XMMATRIX clipToPrevClip = invProj * invView * mPrevViewMatrix * mPrevProjMatrix;
	slConstants.clipToPrevClip = XMMatrixToSLFloat4x4(clipToPrevClip);

	// Previous clip to current clip
	DirectX::XMMATRIX prevClipToClip = DirectX::XMMatrixInverse(nullptr, clipToPrevClip);
	slConstants.prevClipToClip = XMMatrixToSLFloat4x4(prevClipToClip);

	slConstants.jitterOffset = sl::float2(mJitter.x, mJitter.y);

	slConstants.mvecScale = sl::float2(1.0f / static_cast<float>(mRenderWidth), 1.0f / static_cast<float>(mRenderHeight));


	// Camera position and orientation
	slConstants.cameraPos = sl::float3(cameraPos.x, cameraPos.y, cameraPos.z);
	slConstants.cameraUp = sl::float3(0.0f, 1.0f, 0.0f);
	slConstants.cameraRight = sl::float3(1.0f, 0.0f, 0.0f);
	slConstants.cameraFwd = sl::float3(cameraForward.x, cameraForward.y, cameraForward.z);

	// Depth settings
	slConstants.depthInverted = sl::Boolean::eFalse;
	slConstants.cameraMotionIncluded = sl::Boolean::eFalse;
	slConstants.motionVectors3D = sl::Boolean::eFalse;
	slConstants.reset = sl::Boolean::eFalse;

	// Camera near/far
	slConstants.cameraNear = nearZ;
	slConstants.cameraFar = farZ;
	slConstants.cameraFOV = fovY;
	slConstants.cameraAspectRatio = aspectRatio;

	slConstants.cameraPinholeOffset = sl::float2(0.0f, 0.0f);
	slConstants.motionVectorsInvalidValue = FLT_MIN;

	// Set constants
	if (SL_FAILED(res, slSetConstants(slConstants, *frameToken, mSlViewport)))
	{
		std::cerr << "Failed to set DLSS constants. Result was: " << static_cast<int>(res) << std::endl;
		return;
	}

	ID3D12Resource* depthForDLSS = (mCurrentRenderMode == RenderMode::RayTraced)
		? mRTDepthTexture.Get()
		: mDepthBuffer.GetResource();
	// Create Resource objects on the heap (ResourceTag expects pointers)
	sl::Resource depthResource(sl::ResourceType::eTex2d, depthForDLSS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	sl::Resource mvecResource(sl::ResourceType::eTex2d, mGBufferVelocity.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	sl::Resource albedoResource(sl::ResourceType::eTex2d, mOutAlbedoTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	sl::Resource normalResource(sl::ResourceType::eTex2d, mGBufferNormal.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	sl::Resource roughnessResource(sl::ResourceType::eTex2d, mGBufferMaterial.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	sl::Resource emissiveResource(sl::ResourceType::eTex2d, mGBufferEmission.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	//sl::Resource diffuseResource(sl::ResourceType::eTex2d, mOutDiffuseTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	//sl::Resource specularResource(sl::ResourceType::eTex2d, mOutSpecularTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	sl::Resource outputResource(sl::ResourceType::eTex2d, mDLSSOutputTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
	sl::Resource inputColorResource(sl::ResourceType::eTex2d, mCompositeOutputTexture.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
	sl::Resource albedoSpecularResource(sl::ResourceType::eTex2d, mOutAlbedoSpecularTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);

	// Create extent for resources
	sl::Extent renderExtent{ 0, 0, mRenderWidth, mRenderHeight };
	sl::Extent outputExtent{ 0, 0, mWidth, mHeight };

	// Tag resources
	std::vector<sl::ResourceTag> tags = {
		sl::ResourceTag(&depthResource, sl::kBufferTypeDepth, sl::eValidUntilPresent, &renderExtent),
		sl::ResourceTag(&mvecResource, sl::kBufferTypeMotionVectors, sl::eValidUntilPresent, &renderExtent),
		sl::ResourceTag(&albedoResource, sl::kBufferTypeAlbedo, sl::eValidUntilPresent, &renderExtent),
		sl::ResourceTag(&normalResource, sl::kBufferTypeNormals, sl::eValidUntilPresent, &renderExtent),
		sl::ResourceTag(&roughnessResource, sl::kBufferTypeRoughness, sl::eValidUntilPresent, &renderExtent),
		sl::ResourceTag(&emissiveResource, sl::kBufferTypeEmissive, sl::eValidUntilPresent, &renderExtent),
		//		sl::ResourceTag(&diffuseResource, sl::kBufferTypeDiffuseHitNoisy, sl::eValidUntilPresent, &renderExtent),
		//		sl::ResourceTag(&specularResource, sl::kBufferTypeSpecularHitNoisy, sl::eValidUntilPresent, &renderExtent),
				sl::ResourceTag(&outputResource, sl::kBufferTypeScalingOutputColor, sl::eValidUntilPresent, &outputExtent),
				sl::ResourceTag(&inputColorResource, sl::kBufferTypeScalingInputColor, sl::eValidUntilPresent, &renderExtent),
				sl::ResourceTag(&albedoSpecularResource, sl::kBufferTypeSpecularAlbedo, sl::eValidUntilPresent, &renderExtent)
	};

	if (SL_FAILED(res, slSetTagForFrame(*frameToken, mSlViewport, tags.data(), static_cast<uint32_t>(tags.size()), reinterpret_cast<sl::CommandBuffer*>(mCommandList.Get()))))
	{
		std::cerr << "Failed to tag DLSS resources. Result was: " << static_cast<int>(res) << std::endl;
		return;
	}

	const sl::BaseStructure* inputs[] = { &mSlViewport };
	if (SL_FAILED(res, slEvaluateFeature(sl::kFeatureDLSS_RR, *frameToken, inputs, 1, reinterpret_cast<sl::CommandBuffer*>(mCommandList.Get()))))
	{
		std::cerr << "Failed to evaluate DLSS frame. Result was: " << static_cast<int>(res) << std::endl;
		return;
	}
}

void Renderer::CleanupStreamline()
{
	if (mDLSSRREnabled)
	{
		// Free DLSS-RR resources for our viewport
		slFreeResources(sl::kFeatureDLSS_RR, mSlViewport);
		mDLSSRREnabled = false;
		std::cout << "DLSS-RR resources freed." << std::endl;
	}
	if (mStreamlineInitialized)
	{
		slShutdown();
		mStreamlineInitialized = false;
		std::cout << "Streamline shutdown completed." << std::endl;
	}
}

void Renderer::AllocateHandles()
{
	// Tonemap UAV
	mUavHandle_TonemapOutput = mSrvHeap.Allocate();
	// DLSS-RR UAV and SRV
	mUavHandle_DlssOutput = mSrvHeap.Allocate();
	mSrvHandle_DlssOutput = mSrvHeap.Allocate();

	// GBuffer RTVs, SRVs and UAVs
	mRtvHandle_GBufferAlbedo = mRtvHeap.Allocate();
	mRtvHandle_GBufferNormal = mRtvHeap.Allocate();
	mRtvHandle_GBufferMaterial = mRtvHeap.Allocate();
	mRtvHandle_GBufferEmissive = mRtvHeap.Allocate();
	mRtvHandle_GBufferVelocity = mRtvHeap.Allocate();

	mSrvHandle_GBufferAlbedo = mSrvHeap.Allocate();
	mSrvHandle_GBufferNormal = mSrvHeap.Allocate();
	mSrvHandle_GBufferMaterial = mSrvHeap.Allocate();
	mSrvHandle_Depth = mSrvHeap.Allocate();
	mSrvHandle_GBufferEmissive = mSrvHeap.Allocate();
	mSrvHandle_GBufferVelocity = mSrvHeap.Allocate();

	mUavHandle_GBufferNormal = mSrvHeap.Allocate();
	mUavHandle_GBufferEmissive = mSrvHeap.Allocate();
	mUavHandle_GBufferMaterial = mSrvHeap.Allocate();
	mUavHandle_RTDepth = mSrvHeap.Allocate();

	// Composite UAV, RTV, SRV
	mUavHandle_CompositeOutput = mSrvHeap.Allocate();
	mSrvHandle_CompositeOutput = mSrvHeap.Allocate();
	mRtvHandle_CompositeOutput = mRtvHeap.Allocate();

	// Light Buffer SRV
	mSrvHandle_LightBuffer = mSrvHeap.Allocate();

	// DXR UAVs and SRVs
	mUavHandle_Diffuse = mSrvHeap.Allocate();
	mUavHandle_Specular = mSrvHeap.Allocate();
	mUavHandle_Albedo = mSrvHeap.Allocate();
	mUavHandle_AlbedoSpecular = mSrvHeap.Allocate();

	mSrvHandle_Diffuse = mSrvHeap.Allocate();
	mSrvHandle_Specular = mSrvHeap.Allocate();
	mSrvHandle_Albedo = mSrvHeap.Allocate();
	mSrvHandle_AlbedoSpecular = mSrvHeap.Allocate();

	// Phong
	mRtvHandle_PhongOutput = mRtvHeap.Allocate();
	mSrvHandle_PhongOutput = mSrvHeap.Allocate();
}

void Renderer::InitializeDepthBuffer()
{
	mNativeDepthBuffer.Initialize(mDevice.Get(), mWidth, mHeight);

	// DLSS
	mDepthBuffer.Initialize(mDevice.Get(), mRenderWidth, mRenderHeight);

	D3D12_SHADER_RESOURCE_VIEW_DESC depthSrv = {};
	depthSrv.Format = DXGI_FORMAT_R32_FLOAT;
	depthSrv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	depthSrv.Texture2D.MipLevels = 1;
	depthSrv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	mDevice.Get()->CreateShaderResourceView(mDepthBuffer.GetResource(), &depthSrv, mSrvHandle_Depth.cpuHandle);
}

void Renderer::InitializeRayTracingPipelines()
{
	CD3DX12_ROOT_PARAMETER1 localParams[4]{};

	// Param 0: Index buffer (t0)
	localParams[0].InitAsShaderResourceView(0, 1);

	// Param 1: Vertex buffer (t1)
	localParams[1].InitAsShaderResourceView(1, 1);

	// Param 2: Texture table (t2)
	CD3DX12_DESCRIPTOR_RANGE1 texRange{};
	texRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 2, 1); // t0-t4 space1
	localParams[2].InitAsDescriptorTable(1, &texRange);

	localParams[3].InitAsConstants(sizeof(MeshMaterialData) / 4, 0, 1);

	CD3DX12_STATIC_SAMPLER_DESC sampler(0, D3D12_FILTER_ANISOTROPIC); // s0

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC localDesc{};
	localDesc.Init_1_1(4, localParams, 1, &sampler, D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE);

	D3D12RootSignature localRootSig;
	localRootSig.Initialize(mDevice.Get(), localDesc);
	{
		//D3D12RootSignature globalRootSig;
		mRtGlobalRootSignature.InitializeHybridRTGlobalRS(mDevice.Get());

		HLSLShader libraryShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/DeferredRT.hlsl", L"lib_6_5", {}, L"");


		RTPipelineSettings reflSettings;
		reflSettings.maxPayloadSize = sizeof(float) * 8; // Color + Depth
		// 3. Initialize Pipeline
		mReflectionsPipeline.Initialize(mDevice.Get(), &mRtGlobalRootSignature, &localRootSig, libraryShader.GetShaderBlob(), reflSettings);
	}

	{
		// --- FULL RAY TRACING PIPELINE SETUP ---
		mFullRTGlobalRootSignature.InitializeFullRTGlobalRS(mDevice.Get());

		HLSLShader fullRtLibraryShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/FullRT.hlsl", L"lib_6_5", {}, L"");
		//HLSLShader fullRtLibraryShader = mShaderCompiler.LoadFromCso(L"../x64/Debug/FullRT.cso");

		RTPipelineSettings fullRtSettings;
		fullRtSettings.maxPayloadSize = sizeof(float) * 23;
		mFullRTPipeline.Initialize(mDevice.Get(), &mFullRTGlobalRootSignature, &localRootSig, fullRtLibraryShader.GetShaderBlob(), fullRtSettings);
	}

	if (mSceneLoaded)
	{
		// 4. Build Shader Binding Table (SBT)
		std::initializer_list<std::span<const MeshGpuData>> allMeshes =
		{
			mOpaqueSingleSidedMeshes,
			mOpaqueDoubleSidedMeshes,
			mMaskedSingleSidedMeshes,
			mMaskedDoubleSidedMeshes,
			mTransparentSingleSidedMeshes,
			mTransparentDoubleSidedMeshes
		};
		mReflectionsPipeline.BuildSBT(mDevice.Get(), allMeshes);
		mFullRTPipeline.BuildSBT(mDevice.Get(), allMeshes);
	}
}

void Renderer::BuildRayTracingAccelerationStructures()
{
	mCommandList.ResetCommandList(mSwapChain.GetCurrentBackBufferIndex());

	// 1. Build BLAS for all mesh lists
	mRayTracingBuilder.BuildAllBLAS(
		mOpaqueSingleSidedMeshes,
		mOpaqueDoubleSidedMeshes,
		mMaskedSingleSidedMeshes,
		mMaskedDoubleSidedMeshes,
		mTransparentSingleSidedMeshes,
		mTransparentDoubleSidedMeshes);

	// 2. Allocate TLAS instance desc buffer
	UINT totalMeshes = static_cast<UINT>(
		mOpaqueSingleSidedMeshes.size() +
		mOpaqueDoubleSidedMeshes.size() +
		mMaskedSingleSidedMeshes.size() +
		mMaskedDoubleSidedMeshes.size() +
		mTransparentSingleSidedMeshes.size() +
		mTransparentDoubleSidedMeshes.size());

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
		mMaskedSingleSidedMeshes,
		mMaskedDoubleSidedMeshes,
		mTransparentSingleSidedMeshes,
		mTransparentDoubleSidedMeshes,
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

	std::initializer_list<std::span<const MeshGpuData>> allMeshes =
	{
		mOpaqueSingleSidedMeshes,
		mOpaqueDoubleSidedMeshes,
		mMaskedSingleSidedMeshes,
		mMaskedDoubleSidedMeshes,
		mTransparentSingleSidedMeshes,
		mTransparentDoubleSidedMeshes
	};

	mReflectionsPipeline.BuildSBT(mDevice.Get(), allMeshes);
	mFullRTPipeline.BuildSBT(mDevice.Get(), allMeshes);
}

void Renderer::InitializeTonemapPipeline()
{
	HLSLShader tonemapShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/TonemapCS.hlsl", L"cs_6_0");
	mPipelineStateTonemap.InitializeCompute(
		mDevice.Get(),
		mTonemapRootSignature.Get(),
		std::move(tonemapShader));
}

void Renderer::InitializeTonemapResources()
{
	// 1. Create Tonemapped Output Texture (UAV)
	auto uavDesc = CD3DX12_RESOURCE_DESC::Tex2D(
		DXGI_FORMAT_R8G8B8A8_UNORM,
		mWidth,
		mHeight,
		1, // array size
		1, // mip levels
		1, // sample count
		0, // sample quality
		D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS
	);
	mTonemapOutputTexture.Initialize(
		mDevice.Get(),
		uavDesc,
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_RESOURCE_STATE_COMMON);

	// 2. Create UAV Descriptor
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavViewDesc = {};
	uavViewDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	uavViewDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;

	mDevice.Get()->CreateUnorderedAccessView(
		mTonemapOutputTexture.Get(),
		nullptr,
		&uavViewDesc,
		mUavHandle_TonemapOutput.cpuHandle);

	//mDevice.Get()->CreateRenderTargetView(
	//	mTonemapOutputTexture.Get(),
	//	nullptr,
	//	mRtvHandle_.cpuHandle);
}



void Renderer::InitializeRootSignatures()
{
	mMeshRootSignature.InitializeMeshRS(mDevice.Get());
	mComputeRootSignature.InitializeComputeRS(mDevice.Get());
	mCompositeRootSignature.InitializeCompositeRS(mDevice.Get());
	mTonemapRootSignature.InitializeTonemapRS(mDevice.Get());
}

void Renderer::InitializePipelineState()
{
	HLSLShader vertexShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/DeferredVS.hlsl", L"vs_6_0");
	HLSLShader pixelShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/DeferredPS.hlsl", L"ps_6_0");
	HLSLShader computeShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/LightPassCS.hlsl", L"cs_6_5");
	HLSLShader transparentPixelShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/TransparentPixelShader.hlsl", L"ps_6_0");
	HLSLShader compositeShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/CompositeCS.hlsl", L"cs_6_0");

	HLSLShader phongVS = mShaderCompiler.CompileFromFile(L"Source/Shaders/PhongVS.hlsl", L"vs_6_0");
	HLSLShader phongPS = mShaderCompiler.CompileFromFile(L"Source/Shaders/PhongPS.hlsl", L"ps_6_0");

	std::vector<ShaderMacro> maskedDefines = {
		{ L"ALPHA_TEST", L"1" }
	};
	HLSLShader maskedPixelShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/DeferredPS.hlsl", L"ps_6_0", maskedDefines);
	HLSLShader maskedPhongPS = mShaderCompiler.CompileFromFile(L"Source/Shaders/PhongPS.hlsl", L"ps_6_0", maskedDefines);

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
	mPipelineStateTransparentSingle.InitializeTransparent(
		mDevice.Get(),
		mMeshRootSignature.Get(),
		vertexShader,
		transparentPixelShader,
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
		vertexShader,
		std::move(maskedPixelShader),
		inputLayoutDesc,
		true);

	// 6. Transparent double-sided pipeline state
	mPipelineStateTransparentDouble.InitializeTransparent(
		mDevice.Get(),
		mMeshRootSignature.Get(),
		std::move(vertexShader),
		std::move(transparentPixelShader),
		inputLayoutDesc,
		true);


	// 7. Compute pipeline state for deferred
	mPipelineStateCompute.InitializeCompute(
		mDevice.Get(),
		mComputeRootSignature.Get(),
		std::move(computeShader));

	// 8. Composite pipeline state for merging deferred render with reflections
	mPipelineStateComposite.InitializeCompute(
		mDevice.Get(),
		mCompositeRootSignature.Get(),
		std::move(compositeShader));



	// =========================================================================================
	// Phong Pipelines
	// =========================================================================================
	// 1. Phong Opaque pipeline state
	mPipelineStatePhongOpaqueSingle.InitializeForwardOpaque(
		mDevice.Get(),
		mMeshRootSignature.Get(),
		phongVS,
		phongPS,
		inputLayoutDesc);

	// 2. Phong Masked pipeline state (like opaque but with clip)
	mPipelineStatePhongMaskedSingle.InitializeForwardOpaque(
		mDevice.Get(),
		mMeshRootSignature.Get(),
		phongVS,
		maskedPhongPS,
		inputLayoutDesc);

	// 3. Phong Transparent pipeline state
	mPipelineStatePhongTransparentSingle.InitializeForwardTransparent(
		mDevice.Get(),
		mMeshRootSignature.Get(),
		phongVS,
		phongPS,
		inputLayoutDesc);

	// 4. Phong Opaque double-sided pipeline state
	mPipelineStatePhongOpaqueDouble.InitializeForwardOpaque(
		mDevice.Get(),
		mMeshRootSignature.Get(),
		phongVS,
		phongPS,
		inputLayoutDesc,
		true);

	// 5. Phong Masked double-sided pipeline state
	mPipelineStatePhongMaskedDouble.InitializeForwardOpaque(
		mDevice.Get(),
		mMeshRootSignature.Get(),
		phongVS,
		std::move(maskedPhongPS),
		inputLayoutDesc,
		true);

	// 6. Phong Transparent double-sided pipeline state
	mPipelineStatePhongTransparentDouble.InitializeForwardTransparent(
		mDevice.Get(),
		mMeshRootSignature.Get(),
		std::move(phongVS),
		std::move(phongPS),
		inputLayoutDesc,
		true);
}

void Renderer::InitializeTextureLoader()
{
	HLSLShader mipmapShader = mShaderCompiler.CompileFromFile(L"Source/Shaders/MipmapShader.hlsl", L"cs_6_0");
	mTextureLoader.Initialize(mDevice.Get(), &mSrvHeap, &mCommandQueue, &mCommandList, &mUploadHeap, std::move(mipmapShader));
}



void Renderer::Update(const Camera& camera)
{
	// compute delta time
	LARGE_INTEGER now;
	QueryPerformanceCounter(&now);
	double dt = static_cast<double>(now.QuadPart - mPrevCounter.QuadPart) * mSecondsPerCount;
	mPrevCounter = now;

	if (mSceneLoaded)
	{
		DescriptorHandle tonemapInputSrv;
		switch (mCurrentRenderMode)
		{
		case RenderMode::ForwardPhong:
		{
			RenderPhong(camera);
			tonemapInputSrv = mSrvHandle_PhongOutput;
			break;
		}
		case RenderMode::Hybrid:
		{
			RenderHybrid(camera);
			tonemapInputSrv = mSrvHandle_DlssOutput;
			D3D12_RESOURCE_BARRIER barriers[]
			{
				CD3DX12_RESOURCE_BARRIER::Transition(mDLSSOutputTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			};
			mCommandList.Get()->ResourceBarrier(_countof(barriers), barriers);
			break;
		}
		case RenderMode::RayTraced:
		{
			RenderFullRayTraced(camera);
			tonemapInputSrv = mSrvHandle_DlssOutput;
			D3D12_RESOURCE_BARRIER barriers[]
			{
				CD3DX12_RESOURCE_BARRIER::Transition(mDLSSOutputTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
			};
			mCommandList.Get()->ResourceBarrier(_countof(barriers), barriers);
			break;
		}
		}
		// =========================================================================================
		// STAGE 6: ToneMapping (if needed)
		// =========================================================================================
		{
			D3D12_RESOURCE_BARRIER barriers[]
			{
				//CD3DX12_RESOURCE_BARRIER::Transition(mDLSSOutputTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE),
				CD3DX12_RESOURCE_BARRIER::Transition(mTonemapOutputTexture.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_UNORDERED_ACCESS),
			};
			mCommandList.Get()->ResourceBarrier(_countof(barriers), barriers);

			ID3D12DescriptorHeap* heaps[] = { mSrvHeap.Get() };
			mCommandList.Get()->SetDescriptorHeaps(_countof(heaps), heaps);

			// Bind Composite Pipeline
			mCommandList.Get()->SetComputeRootSignature(mTonemapRootSignature.Get());
			mCommandList.Get()->SetPipelineState(mPipelineStateTonemap.Get());
			// Bind Descriptors
			// Slot 0: SRV (t0)
			mCommandList.Get()->SetComputeRootDescriptorTable(0, tonemapInputSrv.gpuHandle);
			// Slot 1: UAV (u0)
			mCommandList.Get()->SetComputeRootDescriptorTable(1, mUavHandle_TonemapOutput.gpuHandle);
			// Dispatch
			mCommandList.Get()->Dispatch((mWidth + 7) / 8, (mHeight + 7) / 8, 1);
		}


		// =========================================================================================
		// STAGE 7: COPY TO BACKBUFFER
		// =========================================================================================
		{
			D3D12_RESOURCE_BARRIER barriers[]
			{
				// Transition Output Texture -> Copy Source
				CD3DX12_RESOURCE_BARRIER::Transition(mTonemapOutputTexture.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE),
				// Transition Back Buffer -> Copy Dest
				CD3DX12_RESOURCE_BARRIER::Transition(mSwapChain.GetCurrentBackBuffer(), D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_COPY_DEST),
			};

			mCommandList.Get()->ResourceBarrier(_countof(barriers), barriers);

			// Copy
			mCommandList.Get()->CopyResource(
				mSwapChain.GetCurrentBackBuffer(),
				mTonemapOutputTexture.Get());

			// Transition Back Buffer -> Present
			D3D12_RESOURCE_BARRIER renderTargetBarrier = CD3DX12_RESOURCE_BARRIER::Transition(
				mSwapChain.GetCurrentBackBuffer(),
				D3D12_RESOURCE_STATE_COPY_DEST,
				D3D12_RESOURCE_STATE_RENDER_TARGET);
			mCommandList.Get()->ResourceBarrier(1, &renderTargetBarrier);

			D3D12_RESOURCE_BARRIER commonCleanup[] =
			{
				CD3DX12_RESOURCE_BARRIER::Transition(mTonemapOutputTexture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
			};
			mCommandList.Get()->ResourceBarrier(_countof(commonCleanup), commonCleanup);


			switch (mCurrentRenderMode)
			{
			case RenderMode::ForwardPhong:
			{
				D3D12_RESOURCE_BARRIER cleanup[]
				{
					CD3DX12_RESOURCE_BARRIER::Transition(mNativeDepthBuffer.GetResource(), D3D12_RESOURCE_STATE_DEPTH_WRITE, D3D12_RESOURCE_STATE_COMMON),
					CD3DX12_RESOURCE_BARRIER::Transition(mPhongOutputTexture.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
				};
				mCommandList.Get()->ResourceBarrier(_countof(cleanup), cleanup);
				break;
			}
			case RenderMode::Hybrid:
			{
				D3D12_RESOURCE_BARRIER cleanup[]
				{
					CD3DX12_RESOURCE_BARRIER::Transition(mGBufferAlbedo.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
					CD3DX12_RESOURCE_BARRIER::Transition(mGBufferNormal.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
					CD3DX12_RESOURCE_BARRIER::Transition(mGBufferMaterial.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
					CD3DX12_RESOURCE_BARRIER::Transition(mGBufferVelocity.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
					//		CD3DX12_RESOURCE_BARRIER::Transition(mTonemapOutputTexture.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, D3D12_RESOURCE_STATE_COMMON),
							CD3DX12_RESOURCE_BARRIER::Transition(mDLSSOutputTexture.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
							//CD3DX12_RESOURCE_BARRIER::Transition(mOutDiffuseTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
							//CD3DX12_RESOURCE_BARRIER::Transition(mOutSpecularTex.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
							CD3DX12_RESOURCE_BARRIER::Transition(mCompositeOutputTexture.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
							CD3DX12_RESOURCE_BARRIER::Transition(mDepthBuffer.GetResource(), D3D12_RESOURCE_STATE_DEPTH_READ, D3D12_RESOURCE_STATE_COMMON),
							CD3DX12_RESOURCE_BARRIER::Transition(mGBufferEmission.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
				};
				mCommandList.Get()->ResourceBarrier(_countof(cleanup), cleanup);
				break;
			}
			case RenderMode::RayTraced:
			{
				D3D12_RESOURCE_BARRIER cleanup[]
				{
					CD3DX12_RESOURCE_BARRIER::Transition(mGBufferVelocity.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
					CD3DX12_RESOURCE_BARRIER::Transition(mGBufferNormal.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
					CD3DX12_RESOURCE_BARRIER::Transition(mDLSSOutputTexture.Get(), D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON),
				};
				mCommandList.Get()->ResourceBarrier(_countof(cleanup), cleanup);
				break;
			}
			}
		}
	}
	else
	{
		mCommandQueue.WaitForFenceInFrame(mSwapChain.GetCurrentBackBufferIndex());
		// Open command list
		mCommandList.ResetCommandList(mSwapChain.GetCurrentBackBufferIndex());

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

	mPrevViewMatrix = camera.GetViewMatrix();
	mPrevProjMatrix = camera.GetProjMatrix();
	mFrameCount++;
	UpdateCameraJitter();
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
	for (auto& mesh : mTransparentSingleSidedMeshes)
	{
		DirectX::XMVECTOR center = DirectX::XMLoadFloat3(&mesh.center);
		DirectX::XMVECTOR camPos = DirectX::XMLoadFloat3(&cameraPos);
		DirectX::XMVECTOR toCamera = DirectX::XMVectorSubtract(camPos, center);
		mesh.distanceToCamera = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(toCamera));
	}

	std::sort(mTransparentSingleSidedMeshes.begin(), mTransparentSingleSidedMeshes.end(),
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
		mMaskedSingleSidedMeshes.size() + mMaskedDoubleSidedMeshes.size() +
		mTransparentSingleSidedMeshes.size();

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
		BuildRayTracingAccelerationStructures();
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
		mMaskedSingleSidedMeshes.size() + mMaskedDoubleSidedMeshes.size() +
		mTransparentSingleSidedMeshes.size() + mTransparentDoubleSidedMeshes.size();
	size_t addedMeshCount = newMeshCount - previousMeshCount;

	wcout << L"Extension scene(s) added successfully! (Total: " << mModels.size() << L" model(s), "
		<< newMeshCount << L" meshes, added: " << addedMeshCount << L" meshes)" << endl;
	return true;
}
