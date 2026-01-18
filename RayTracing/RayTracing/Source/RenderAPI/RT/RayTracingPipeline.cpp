#include "pch.h"
#include "helpers.h"
#include "RenderAPI/Renderer.h"
#include "RayTracingPipeline.h"

// In RayTracingPipeline.cpp

UINT RayTracingPipeline::GetShaderIdentifierSize(ID3D12Device5* pDevice)
{
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 options = {};
	pDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &options, sizeof(options));
	return options.RaytracingTier < D3D12_RAYTRACING_TIER_1_0 ? 0 : D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
}

void RayTracingPipeline::Initialize(ID3D12Device5* pDevice, D3D12RootSignature* globalSig, D3D12RootSignature* localSig, IDxcBlob* shaderBlob, const RTPipelineSettings& settings)
{
	mSettings = settings;
	// --- 1. Define the Pipeline ---
	CD3DX12_STATE_OBJECT_DESC pipelineDesc(D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE);

	// A. Shader Library (Compile 'DeferredRT.hlsl' to lib_6_5)
	// You need to load this blob via your ShaderCompiler
	auto lib = pipelineDesc.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
	CD3DX12_SHADER_BYTECODE shaderBytecode(shaderBlob->GetBufferPointer(), shaderBlob->GetBufferSize());
	lib->SetDXILLibrary(&shaderBytecode);

	// Define the exports (entry points) from the library
	lib->DefineExport(mSettings.rayGenShader.c_str());
	lib->DefineExport(mSettings.missShader.c_str());
	lib->DefineExport(mSettings.shadowMissShader.c_str());
	lib->DefineExport(mSettings.closestHit.c_str());
	lib->DefineExport(mSettings.closestHitTransparent.c_str());
	lib->DefineExport(mSettings.anyHit.c_str());
	lib->DefineExport(mSettings.anyHitTransparent.c_str());
	lib->DefineExport(mSettings.anyHitDecal.c_str());

	// B. Hit Groups
	// Combines ClosestHit and AnyHit into one named group "HitGroup"
	auto hitGroup = pipelineDesc.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
	hitGroup->SetClosestHitShaderImport(mSettings.closestHit.c_str());
	hitGroup->SetAnyHitShaderImport(mSettings.anyHit.c_str());
	hitGroup->SetHitGroupExport(mSettings.hitGroup.c_str());
	hitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

	// Transparent Double-Sided Hit Group	
	auto hitGroupTransparentDouble = pipelineDesc.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
	hitGroupTransparentDouble->SetClosestHitShaderImport(mSettings.closestHitTransparent.c_str());
	hitGroupTransparentDouble->SetAnyHitShaderImport(mSettings.anyHitTransparent.c_str());
	hitGroupTransparentDouble->SetHitGroupExport(mSettings.hitGroupTransparentDouble.c_str());
	hitGroupTransparentDouble->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

	// Transparent Single-Sided Hit Group
	auto hitGroupTransparentSingle = pipelineDesc.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
	//hitGroupTransparentSingle->SetClosestHitShaderImport(mSettings.closestHitTransparent.c_str());
	hitGroupTransparentSingle->SetAnyHitShaderImport(mSettings.anyHitDecal.c_str());
	hitGroupTransparentSingle->SetHitGroupExport(mSettings.hitGroupTransparentSingle.c_str());
	hitGroupTransparentSingle->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

	// C. Root Signatures
	// Global: Bound once (Output UAV, TLAS, G-Buffer)
	auto globalRoot = pipelineDesc.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
	globalRoot->SetRootSignature(globalSig->Get());

	// Local: Bound per geometry (Material Constants, Textures)
	auto localRoot = pipelineDesc.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
	localRoot->SetRootSignature(localSig->Get());

	// Associate Local Root Sig with the Hit Group
	auto rootAssoc = pipelineDesc.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
	rootAssoc->SetSubobjectToAssociate(*localRoot);
	rootAssoc->AddExport(mSettings.hitGroup.c_str());
	rootAssoc->AddExport(mSettings.hitGroupTransparentDouble.c_str());
	rootAssoc->AddExport(mSettings.hitGroupTransparentSingle.c_str());

	// D. Config
	auto shaderConfig = pipelineDesc.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
	shaderConfig->Config(mSettings.maxPayloadSize, mSettings.maxAttributeSize);


	UINT maxRecursion = mSettings.maxRecursion + 2;
	auto pipelineConfig = pipelineDesc.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG1_SUBOBJECT>();
	pipelineConfig->Config(maxRecursion, D3D12_RAYTRACING_PIPELINE_FLAG_SKIP_PROCEDURAL_PRIMITIVES);


	// Create
	HRESULT hr = pDevice->CreateStateObject(pipelineDesc, IID_PPV_ARGS(&mStateObject));
	ASSERT_HR(hr, "Failed to create DXR Pipeline");
}


void RayTracingPipeline::BuildSBT(ID3D12Device5* pDevice, const std::initializer_list<std::span<const MeshGpuData>>& meshes)
{
	// 1. Calculate Total Mesh Count for sizing
	size_t totalMeshes = 0;
	for (const auto& meshSpan : meshes)
		totalMeshes += meshSpan.size();

	// 2. Define Record Sizes
	UINT shaderIDSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;
	UINT materialSize = sizeof(MeshMaterialData);
	// ID + 3 GPU Addresses (Index, Vertex, Texture) + Material Constants
	UINT recordSize = (shaderIDSize + 8 * 3 + materialSize + 31) & ~31; // Align to 32 bytes

	mRayGenSectionSize = recordSize;    // 1 RayGen record
	mMissSectionSize = recordSize * 2;  // 2 Miss records
	mHitGroupSectionSize = recordSize * static_cast<UINT>(totalMeshes); // Contiguous block

	UINT sbtSize = (mRayGenSectionSize + mMissSectionSize + mHitGroupSectionSize + 255) & ~255;

	// 3. Initialize/Re-initialize SBT Buffer
	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
	auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sbtSize);
	mSBTStorage.Initialize(pDevice, bufferDesc, heapProps);

	// 4. Map the buffer to pData (Fixes the undefined error)
	uint8_t* pData = nullptr;
	mSBTStorage.Get()->Map(0, nullptr, (void**)&pData);

	Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> props;
	mStateObject.As(&props);

	// 5. Write RayGen and Miss Shaders
	memcpy(pData, props->GetShaderIdentifier(mSettings.rayGenShader.c_str()), shaderIDSize);
	pData += mRayGenSectionSize;

	memcpy(pData, props->GetShaderIdentifier(mSettings.missShader.c_str()), shaderIDSize);
	pData += mMissSectionSize / 2;

	memcpy(pData, props->GetShaderIdentifier(mSettings.shadowMissShader.c_str()), shaderIDSize);
	pData += mMissSectionSize / 2;

	// 6. Write Hit Groups (Contiguous for Single BLAS)
	void* opaqueHG = props->GetShaderIdentifier(mSettings.hitGroup.c_str());
	void* transDoubleHG = props->GetShaderIdentifier(mSettings.hitGroupTransparentDouble.c_str());
	void* transSingleHG = props->GetShaderIdentifier(mSettings.hitGroupTransparentSingle.c_str());

	int listIndex = 0;
	for (const auto& meshSpan : meshes)
	{
		// Select the shader ID based on the bucket order
		void* currentHitGroupID = opaqueHG;
		if (listIndex == 4)      currentHitGroupID = transSingleHG; // Decals (Transparent Single-Sided)
		else if (listIndex == 5) currentHitGroupID = transDoubleHG; // Glass (Transparent Double-Sided)

		for (const auto& mesh : meshSpan)
		{
			uint8_t* pRecordStart = pData;

			memcpy(pData, currentHitGroupID, shaderIDSize);
			pData += shaderIDSize;

			D3D12_GPU_VIRTUAL_ADDRESS ibAddr = mesh.ib.Get()->GetGPUVirtualAddress();
			D3D12_GPU_VIRTUAL_ADDRESS vbAddr = mesh.vb.Get()->GetGPUVirtualAddress();
			memcpy(pData, &ibAddr, 8); pData += 8;
			memcpy(pData, &vbAddr, 8); pData += 8;

			memcpy(pData, &mesh.materialTable.gpuHandle, 8); pData += 8;
			memcpy(pData, &mesh.materialData, sizeof(MeshMaterialData));

			pData = pRecordStart + recordSize;
		}
		listIndex++;
	}

	mSBTStorage.Get()->Unmap(0, nullptr);
}

//void RayTracingPipeline::BuildSBT(ID3D12Device5* pDevice, const std::initializer_list<std::span<const MeshGpuData>>& meshes)
//{
//	size_t totalMeshes = 0;
//	for (const auto& meshSpan : meshes)
//		totalMeshes += meshSpan.size();
//
//	// Hit Group Record = [Shader ID (32B)] + [IndexBuffer Ptr (8B)] + [VertexBuffer Ptr (8B)] + [Texture Handle (8B)]
//	UINT shaderIDSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES; // 32
//	UINT materialSize = sizeof(MeshMaterialData);
//	UINT recordSize = shaderIDSize + 8 * 3 + materialSize; // ID + 1 GPU Descriptor Handle (64-bit)
//	recordSize = (recordSize + 31) & ~31; // Align to 32 bytes
//
//	mRayGenSectionSize = recordSize;    // 1 RayGen shader
//	mMissSectionSize = recordSize * 2;  // 2 Miss shaders (Color + Shadow)
//	mHitGroupSectionSize = recordSize * static_cast<UINT>(totalMeshes); // 1 HitGroup per mesh
//
//	// Total Size: 1 RayGen + 2 Miss (Color/Shadow) + N HitGroups (one per mesh)
//	UINT sbtSize = mRayGenSectionSize + mMissSectionSize + mHitGroupSectionSize;
//	sbtSize = (sbtSize + 255) & ~255; // Align to 256 bytes
//
//	// Create the SBT Buffer
//	auto heapProps = CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD);
//	auto bufferDesc = CD3DX12_RESOURCE_DESC::Buffer(sbtSize);
//	mSBTStorage.Initialize(pDevice, bufferDesc, heapProps);
//
//	// Map Buffer
//	uint8_t* pData = nullptr;
//	mSBTStorage.Get()->Map(0, nullptr, (void**)&pData);
//
//	Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> props;
//	mStateObject.As(&props);
//
//	// 1. Write RayGen
//	memcpy(pData, props->GetShaderIdentifier(mSettings.rayGenShader.c_str()), shaderIDSize);
//	pData += mRayGenSectionSize;
//
//	// 2. Write Miss
//	memcpy(pData, props->GetShaderIdentifier(mSettings.missShader.c_str()), shaderIDSize);
//	pData += mMissSectionSize / 2;
//
//	// 3. Write Shadow Miss
//	memcpy(pData, props->GetShaderIdentifier(mSettings.shadowMissShader.c_str()), shaderIDSize);
//	pData += mMissSectionSize / 2;
//
//	// 4. Write Hit Groups (Per Mesh)
//	void* opaqueHitGroup = props->GetShaderIdentifier(mSettings.hitGroup.c_str());
//	void* transDoubleHitGroup = props->GetShaderIdentifier(mSettings.hitGroupTransparentDouble.c_str());
//	void* transSingleHitGroup = props->GetShaderIdentifier(mSettings.hitGroupTransparentSingle.c_str());
//
//	int listIndex = 0;
//	for (const auto& meshSpan : meshes)
//	{
//		void* currentHitGroupInfo = opaqueHitGroup;
//		if (listIndex == 4)
//			currentHitGroupInfo = transSingleHitGroup;
//		else if (listIndex == 5)
//			currentHitGroupInfo = transDoubleHitGroup;
//
//		for (const auto& mesh : meshSpan)
//		{
//			uint8_t* pDataStart = pData;
//
//			// A. Copy Shader ID for "HitGroup"
//			memcpy(pData, currentHitGroupInfo, shaderIDSize);
//			pData += shaderIDSize;
//
//			// B. Copy Root Argument: The GPU Pointer to the Index Buffer
//
//			// Arg 0: Index Buffer GPU Address (8 bytes)
//			auto indexBufferAddr = mesh.ib.Get()->GetGPUVirtualAddress();
//			memcpy(pData, &indexBufferAddr, sizeof(indexBufferAddr));
//			pData += sizeof(indexBufferAddr);
//
//			// Arg 1: Vertex Buffer GPU Address (8 bytes)
//			auto vertexBufferAddr = mesh.vb.Get()->GetGPUVirtualAddress();
//			memcpy(pData, &vertexBufferAddr, sizeof(vertexBufferAddr));
//			pData += sizeof(vertexBufferAddr);
//
//			// Arg 2: Texture Descriptor Table Handle (8 bytes)
//			auto textureHandle = mesh.materialTable.gpuHandle;
//			memcpy(pData, &textureHandle, sizeof(textureHandle));
//
//			// Arg 3: Material Data (MeshMaterialData)
//			pData += sizeof(textureHandle);
//			memcpy(pData, &mesh.materialData, sizeof(MeshMaterialData));
//
//			pData = pDataStart + recordSize; // Advance to next record (considering alignment)
//		}
//		listIndex++;
//	}
//
//	mSBTStorage.Get()->Unmap(0, nullptr);
//}

void RayTracingPipeline::Dispatch(ID3D12GraphicsCommandList4* pCmd, UINT width, UINT height)
{
	// 1. Set the Pipeline State
	pCmd->SetPipelineState1(mStateObject.Get());

	// 2. Setup Dispatch Description (Pointing to the SBT)
	D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};

	// Base address of the SBT buffer
	auto sbtAddress = mSBTStorage.Get()->GetGPUVirtualAddress();

	// A. Ray Generation Shader Record
	// It's the first record in our buffer.
	dispatchDesc.RayGenerationShaderRecord.StartAddress = sbtAddress;
	dispatchDesc.RayGenerationShaderRecord.SizeInBytes = mRayGenSectionSize; // Should be aligned to 32 bytes

	// B. Miss Shader Table
	// Starts after RayGen. 
	dispatchDesc.MissShaderTable.StartAddress = sbtAddress + mRayGenSectionSize;
	dispatchDesc.MissShaderTable.SizeInBytes = mMissSectionSize;
	dispatchDesc.MissShaderTable.StrideInBytes = mMissSectionSize / 2;// / 2; // Assuming 2 Miss Shaders (Regular + Shadow), uniform stride

	// C. Hit Group Table
	// Starts after Miss Table.
	dispatchDesc.HitGroupTable.StartAddress = sbtAddress + mRayGenSectionSize + mMissSectionSize;
	dispatchDesc.HitGroupTable.SizeInBytes = mHitGroupSectionSize;

	UINT materialSize = sizeof(MeshMaterialData);
	dispatchDesc.HitGroupTable.StrideInBytes = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES + 8 * 3 + materialSize;

	// NOTE: Stride MUST be aligned to 32 bytes! 
	// (32 + 24 = 56). Next multiple of 32 is 64.
	// Ensure BuildSBT aligned the stride to 64!
	dispatchDesc.HitGroupTable.StrideInBytes = (dispatchDesc.HitGroupTable.StrideInBytes + 31) & ~31;

	// D. Dimensions
	dispatchDesc.Width = width;
	dispatchDesc.Height = height;
	dispatchDesc.Depth = 1;

	// 3. Launch!
	pCmd->DispatchRays(&dispatchDesc);
}
