#include "pch.h"
#include "RayTracingBuilder.h"
#include "RenderAPI/Renderer.h"



static constexpr UINT64 MAX_SCRATCH_SIZE = 256 * 1024 * 1024; // 256 MB

void RayTracingBuilder::Initialize(ID3D12Device5* pDevice, ID3D12GraphicsCommandList4* pCmdList, D3D12CommandQueue* pQueue)
{
	mDevice = pDevice;
	mCmdList = pCmdList;
	mQueue = pQueue;
}
void RayTracingBuilder::BuildSingleGlobalBLAS(
	std::vector<MeshGpuData>& opaqueSingle,
	std::vector<MeshGpuData>& opaqueDouble,
	std::vector<MeshGpuData>& maskedSingle,
	std::vector<MeshGpuData>& maskedDouble,
	std::vector<MeshGpuData>& transparentSingle,
	std::vector<MeshGpuData>& transparentDouble,
	D3D12Resource& outBlasResult)
{
	std::vector<D3D12_RAYTRACING_GEOMETRY_DESC> allGeoms;

	// Helper to add a whole bucket to the descriptor list
	auto AddToGeoms = [&](std::vector<MeshGpuData>& meshes, bool isOpaque) {
		for (auto& mesh : meshes)
		{
			D3D12_RAYTRACING_GEOMETRY_DESC geom = {};
			geom.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
			// Mark as Opaque to skip AnyHit entirely for performance
			geom.Flags = isOpaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;

			geom.Triangles.VertexBuffer.StartAddress = mesh.vb.Get()->GetGPUVirtualAddress();
			geom.Triangles.VertexBuffer.StrideInBytes = mesh.vbv.StrideInBytes;
			geom.Triangles.VertexCount = mesh.vbv.SizeInBytes / mesh.vbv.StrideInBytes;
			geom.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;

			geom.Triangles.IndexBuffer = mesh.ib.Get()->GetGPUVirtualAddress();
			geom.Triangles.IndexCount = mesh.ibv.SizeInBytes / (mesh.ibv.Format == DXGI_FORMAT_R16_UINT ? 2 : 4);
			geom.Triangles.IndexFormat = mesh.ibv.Format;

			allGeoms.push_back(geom);
		}
		};

	// Collect all geometries
	AddToGeoms(opaqueSingle, true);
	AddToGeoms(opaqueDouble, true);
	AddToGeoms(maskedSingle, false);
	AddToGeoms(maskedDouble, false);
	AddToGeoms(transparentSingle, false);
	AddToGeoms(transparentDouble, false);

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	inputs.NumDescs = (UINT)allGeoms.size();
	inputs.pGeometryDescs = allGeoms.data();
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
	mDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

	// Allocate result and scratch
	auto resultDesc = CD3DX12_RESOURCE_DESC::Buffer(info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	outBlasResult.Initialize(mDevice, resultDesc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

	D3D12Resource scratch;
	auto scratchDesc = CD3DX12_RESOURCE_DESC::Buffer(info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
	scratch.Initialize(mDevice, scratchDesc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);

	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
	buildDesc.Inputs = inputs;
	buildDesc.DestAccelerationStructureData = outBlasResult.Get()->GetGPUVirtualAddress();
	buildDesc.ScratchAccelerationStructureData = scratch.Get()->GetGPUVirtualAddress();

	mCmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

	D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(outBlasResult.Get());
	mCmdList->ResourceBarrier(1, &uavBarrier);

	mTempResources.push_back(std::move(scratch)); // Keep scratch alive during build
}

void RayTracingBuilder::BuildSingleGlobalTLAS(
	const D3D12Resource& unifiedBlas, // The single BLAS we built
	UINT totalGeomCount,              // Total number of geometries in that BLAS
	D3D12Resource& tlasResultBuffer,
	D3D12Resource& tlasScratchBuffer,
	D3D12Resource& instanceDescsBuffer)
{
	// We only need ONE instance to point to the single BLAS.
	// However, if you want different masks for different parts, 
	// you would still need multiple instances with different 'InstanceContributionToHitGroupIndex'.
	// For a "Single BLAS = Single Instance" approach:

	D3D12_RAYTRACING_INSTANCE_DESC desc = {};
	desc.Transform[0][0] = desc.Transform[1][1] = desc.Transform[2][2] = 1.0f;
	desc.InstanceID = 0;
	desc.InstanceMask = 0xFF; // All rays hit
	desc.InstanceContributionToHitGroupIndex = 0; // Starts at index 0 in the SBT
	desc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE; // Individual cull flags handled in BLAS
	desc.AccelerationStructure = unifiedBlas.Get()->GetGPUVirtualAddress();

	std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances = { desc };

	// 1. Upload Instances to GPU
	UINT dataSize = (UINT)instances.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
	void* pData;
	instanceDescsBuffer.Get()->Map(0, nullptr, &pData);
	memcpy(pData, instances.data(), dataSize);
	instanceDescsBuffer.Get()->Unmap(0, nullptr);

	// 2. Setup Inputs and Get Sizes
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.NumDescs = (UINT)instances.size();
	inputs.InstanceDescs = instanceDescsBuffer.Get()->GetGPUVirtualAddress();

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
	mDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

	{
		// Allocate the actual TLAS buffer
		D3D12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Buffer(info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		tlasResultBuffer.Initialize(mDevice, resDesc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

		// Allocate the scratch buffer required for the build
		D3D12_RESOURCE_DESC scratchDesc = CD3DX12_RESOURCE_DESC::Buffer(info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		tlasScratchBuffer.Initialize(mDevice, scratchDesc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
	}

	// 3. Build & Barrier
	// (Allocate tlasResultBuffer and tlasScratchBuffer as before)
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
	buildDesc.Inputs = inputs;
	buildDesc.DestAccelerationStructureData = tlasResultBuffer.Get()->GetGPUVirtualAddress();
	buildDesc.ScratchAccelerationStructureData = tlasScratchBuffer.Get()->GetGPUVirtualAddress();

	mCmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);
	D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(tlasResultBuffer.Get());
	mCmdList->ResourceBarrier(1, &uavBarrier);
}

void RayTracingBuilder::BuildAllBLAS(
	std::vector<MeshGpuData>& opaqueSingle,
	std::vector<MeshGpuData>& opaqueDouble,
	std::vector<MeshGpuData>& maskedSingle,
	std::vector<MeshGpuData>& maskedDouble,
	std::vector<MeshGpuData>& transparentSingle,
	std::vector<MeshGpuData>& transparentDouble)
{
	std::vector<BlasBuildReq> buildRequests;
	size_t totalCount = opaqueSingle.size() + opaqueDouble.size() + maskedSingle.size() + maskedDouble.size() + transparentSingle.size() + transparentDouble.size();
	buildRequests.reserve(totalCount);

	auto queueMeshes = [&](std::vector<MeshGpuData>& meshes, bool isOpaque)
		{
			for (auto& mesh : meshes)
			{
				BlasBuildReq req{};
				req.mesh = &mesh;

				// Describe geometry
				req.geomDesc = {};
				req.geomDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
				req.geomDesc.Flags = isOpaque ? D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE : D3D12_RAYTRACING_GEOMETRY_FLAG_NONE;

				req.geomDesc.Triangles.VertexBuffer.StartAddress = mesh.vb.Get()->GetGPUVirtualAddress();
				req.geomDesc.Triangles.VertexBuffer.StrideInBytes = mesh.vbv.StrideInBytes;
				req.geomDesc.Triangles.VertexCount = mesh.vbv.SizeInBytes / mesh.vbv.StrideInBytes;
				req.geomDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT; // Position only

				req.geomDesc.Triangles.IndexBuffer = mesh.ib.Get()->GetGPUVirtualAddress();
				req.geomDesc.Triangles.IndexCount = mesh.ibv.SizeInBytes / (mesh.ibv.Format == DXGI_FORMAT_R16_UINT ? 2 : 4);
				req.geomDesc.Triangles.IndexFormat = mesh.ibv.Format;
				req.geomDesc.Triangles.Transform3x4 = 0; // No per-mesh transform


				D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS buildInputs{};
				buildInputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
				buildInputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
				buildInputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
				buildInputs.NumDescs = 1;
				buildInputs.pGeometryDescs = &req.geomDesc;

				mDevice->GetRaytracingAccelerationStructurePrebuildInfo(&buildInputs, &req.info);

				buildRequests.push_back(req);
			}
		};

	queueMeshes(opaqueSingle, true);
	queueMeshes(opaqueDouble, true);
	queueMeshes(maskedSingle, false);
	queueMeshes(maskedDouble, false);
	queueMeshes(transparentSingle, false);
	queueMeshes(transparentDouble, false);


	if (buildRequests.empty())
		return;

	// Process in batches
	size_t currentIndex = 0;
	while (currentIndex < buildRequests.size())
	{
		size_t batchStart = currentIndex;
		UINT64 batchScratchSize = 0;
		size_t batchCount = 0;

		// Step A: Determine batch size
		while (currentIndex < buildRequests.size())
		{
			UINT64 reqSize = buildRequests[currentIndex].info.ScratchDataSizeInBytes;
			reqSize = (reqSize + 255) & ~255; // Align to 256 bytes

			if (batchScratchSize > 0 && (batchScratchSize + reqSize) > MAX_SCRATCH_SIZE)
				break; // Batch full

			batchScratchSize += reqSize;
			currentIndex++;
			batchCount++;
		}

		// Step B: Allocate scratch for batch
		D3D12Resource scratchBuffer;
		{
			D3D12_RESOURCE_DESC desc = CD3DX12_RESOURCE_DESC::Buffer(batchScratchSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			D3D12_HEAP_PROPERTIES heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT);
			scratchBuffer.Initialize(mDevice, desc, heapProps.Type, D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
		}

		// Step C: Build BLAS for batch
		UINT64 currentScratchOffset = 0;
		D3D12_GPU_VIRTUAL_ADDRESS scratchBase = scratchBuffer.Get()->GetGPUVirtualAddress();
		std::vector<D3D12_RESOURCE_BARRIER> uavBarriers;
		uavBarriers.reserve(batchCount);

		for (size_t i = batchStart; i < (batchStart + batchCount); i++)
		{
			auto& req = buildRequests[i];

			// Allocate BLAS result buffer
			auto resultDesc = CD3DX12_RESOURCE_DESC::Buffer(req.info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
			req.mesh->blasResult.Initialize(mDevice, resultDesc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

			// Setup build desc
			D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc{};
			buildDesc.Inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
			buildDesc.Inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
			buildDesc.Inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
			buildDesc.Inputs.NumDescs = 1;
			buildDesc.Inputs.pGeometryDescs = &req.geomDesc;

			buildDesc.DestAccelerationStructureData = req.mesh->blasResult.Get()->GetGPUVirtualAddress();
			buildDesc.ScratchAccelerationStructureData = scratchBase + currentScratchOffset;


			// Queue the build
			mCmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

			// Make it visible for TLAS build
			uavBarriers.push_back(CD3DX12_RESOURCE_BARRIER::UAV(req.mesh->blasResult.Get()));

			// Advance scratch offset
			UINT64 scratchSize = req.info.ScratchDataSizeInBytes;
			scratchSize = (scratchSize + 255) & ~255; // Align to 256 bytes
			currentScratchOffset += scratchSize;
		}

		mCmdList->ResourceBarrier(static_cast<UINT>(uavBarriers.size()), uavBarriers.data());

		// The 'scratchBuffer' smart pointer will release the resource when going out of scope
		// Make sure it lives until GPU is done
		mTempResources.push_back(std::move(scratchBuffer));
	}
}

void RayTracingBuilder::BuildTLAS(
	const std::vector<MeshGpuData>& opaqueSingle,
	const std::vector<MeshGpuData>& opaqueDouble,
	const std::vector<MeshGpuData>& maskedSingle,
	const std::vector<MeshGpuData>& maskedDouble,
	const std::vector<MeshGpuData>& transparentSingle,
	const std::vector<MeshGpuData>& transparentDouble,
	D3D12Resource& tlasResultBuffer,
	D3D12Resource& tlasScratchBuffer,
	D3D12Resource& instanceDescsBuffer)
{
	std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances;
	UINT instanceID = 0;

	constexpr UINT MASK_SHADOW_CASTER = 0xFF;
	constexpr UINT MASK_NO_SHADOW = 0xFE;
	auto AddInstances = [&](const std::vector<MeshGpuData>& list, UINT flags, UINT instanceMask)
		{
			for (const auto& mesh : list)
			{
				D3D12_RAYTRACING_INSTANCE_DESC desc = {};
				// Identity Transform (3x4 matrix row-major)
				desc.Transform[0][0] = desc.Transform[1][1] = desc.Transform[2][2] = 1.0f;

				desc.InstanceID = instanceID; // Maps to InstanceIndex() in HLSL
				desc.InstanceMask = instanceMask;
				desc.InstanceContributionToHitGroupIndex = instanceID; // 0 for Opaque, we might change this for Masked?

				// Flags can override Geometry flags
				// e.g., D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE
				// But we handled flags in BLAS build, so NONE is fine here usually.
				desc.Flags = flags;

				desc.AccelerationStructure = mesh.blasResult.Get()->GetGPUVirtualAddress();

				instances.push_back(desc);
				instanceID++;
			}
		};

	// Add buckets with appropriate flags

	// 1. Opaque Single-Sided -> HitGroup 0, Force Opaque
	AddInstances(opaqueSingle, D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE, MASK_SHADOW_CASTER);

	// 2. Opaque Double-Sided -> HitGroup 0, Force Opaque + Cull Disabled
	AddInstances(opaqueDouble, D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE | D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE, MASK_SHADOW_CASTER);

	// 3. Masked Single-Sided -> HitGroup 1 (Any hit), No special flags
	AddInstances(maskedSingle, D3D12_RAYTRACING_INSTANCE_FLAG_NONE, MASK_SHADOW_CASTER);

	// 4. Masked Double-Sided -> HitGroup 1 (Any hit), Cull Disabled
	AddInstances(maskedDouble, D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE, MASK_SHADOW_CASTER);

	// 5. Transparent -> HitGroup 2 (Glass Logic), Cull Disabled TODO: Different hit group?
	AddInstances(transparentSingle, D3D12_RAYTRACING_INSTANCE_FLAG_NONE, MASK_NO_SHADOW);
	AddInstances(transparentDouble, D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE, MASK_NO_SHADOW);


	// 1. Upload Instances to GPU
	UINT dataSize = (UINT)instances.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
	void* pData;
	instanceDescsBuffer.Get()->Map(0, nullptr, &pData);
	memcpy(pData, instances.data(), dataSize);
	instanceDescsBuffer.Get()->Unmap(0, nullptr);

	// 2. Get Sizes
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	inputs.Flags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.NumDescs = (UINT)instances.size();
	inputs.InstanceDescs = instanceDescsBuffer.Get()->GetGPUVirtualAddress();

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO info;
	mDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &info);

	// 3. Allocate Buffers
	{
		D3D12_RESOURCE_DESC resDesc = CD3DX12_RESOURCE_DESC::Buffer(info.ResultDataMaxSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		tlasResultBuffer.Initialize(mDevice, resDesc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE);

		D3D12_RESOURCE_DESC scratchDesc = CD3DX12_RESOURCE_DESC::Buffer(info.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS);
		tlasScratchBuffer.Initialize(mDevice, scratchDesc, D3D12_HEAP_TYPE_DEFAULT, D3D12_RESOURCE_STATE_COMMON);
	}

	// 4. Build
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
	buildDesc.Inputs = inputs;
	buildDesc.DestAccelerationStructureData = tlasResultBuffer.Get()->GetGPUVirtualAddress();
	buildDesc.ScratchAccelerationStructureData = tlasScratchBuffer.Get()->GetGPUVirtualAddress();

	mCmdList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

	// 5. Barrier
	D3D12_RESOURCE_BARRIER uavBarrier = CD3DX12_RESOURCE_BARRIER::UAV(tlasResultBuffer.Get());
	mCmdList->ResourceBarrier(1, &uavBarrier);
}


