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

void RayTracingBuilder::BuildAllBLAS(
	std::vector<MeshGpuData>& opaqueSingle, 
	std::vector<MeshGpuData>& opaqueDouble, 
	std::vector<MeshGpuData>& maskedSingle, 
	std::vector<MeshGpuData>& maskedDouble, 
	std::vector<MeshGpuData>& transparent)
{
	std::vector<BlasBuildReq> buildRequests;
	size_t totalCount = opaqueSingle.size() + opaqueDouble.size() + maskedSingle.size() + maskedDouble.size() + transparent.size();
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
	queueMeshes(transparent, false);

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
	const std::vector<MeshGpuData>& transparent,
	D3D12Resource& tlasResultBuffer,
	D3D12Resource& tlasScratchBuffer,
	D3D12Resource& instanceDescsBuffer)
{
	std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instances;
	UINT instanceID = 0;

	auto AddInstances = [&](const std::vector<MeshGpuData>& list, UINT flags, UINT hitGroupIndex)
		{
			for (const auto& mesh : list)
			{
				D3D12_RAYTRACING_INSTANCE_DESC desc = {};
				// Identity Transform (3x4 matrix row-major)
				desc.Transform[0][0] = desc.Transform[1][1] = desc.Transform[2][2] = 1.0f;

				desc.InstanceID = instanceID; // Maps to InstanceIndex() in HLSL
				desc.InstanceMask = 0xFF;     // Visible to all rays
				desc.InstanceContributionToHitGroupIndex = 0; // 0 for Opaque, we might change this for Masked?

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
	AddInstances(opaqueSingle, D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE, 0);

	// 2. Opaque Double-Sided -> HitGroup 0, Force Opaque + Cull Disabled
	AddInstances(opaqueDouble, D3D12_RAYTRACING_INSTANCE_FLAG_FORCE_OPAQUE | D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE, 0);

	// 3. Masked Single-Sided -> HitGroup 1 (Any hit), No special flags
	AddInstances(maskedSingle, D3D12_RAYTRACING_INSTANCE_FLAG_NONE, 1);

	// 4. Masked Double-Sided -> HitGroup 1 (Any hit), Cull Disabled
	AddInstances(maskedDouble, D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE, 1);

	// 5. Transparent -> HitGroup 2 (Glass Logic), Cull Disabled TODO: Different hit group?
	AddInstances(transparent, D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_CULL_DISABLE, 2);

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


