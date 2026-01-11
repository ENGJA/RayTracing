#pragma once
#include "RenderAPI/D3D12/D3D12RootSignature.h"
#include "RenderAPI/HLSL/HLSLShader.h"
#include "RenderAPI/D3D12/D3D12Resource.h"

struct MeshGpuData;


struct RTPipelineSettings
{
	std::wstring rayGenShader = L"RayGen";
	std::wstring missShader = L"Miss";
	std::wstring shadowMissShader = L"ShadowMiss";
	std::wstring hitGroup = L"HitGroup";
	std::wstring hitGroupTransparentSingle = L"HitGroupTransparentSingle";
	std::wstring hitGroupTransparentDouble = L"HitGroupTransparentDouble";
	std::wstring closestHit = L"ClosestHit";
	std::wstring closestHitTransparent = L"ClosestHitTransparent";
	std::wstring anyHit = L"AnyHit";
	std::wstring anyHitTransparent = L"AnyHitTransparent";

	// Limits
	UINT maxPayloadSize = sizeof(float) * 4; // 16 bytes (Color + Depth) or larger
	UINT maxAttributeSize = sizeof(float) * 2; // Barycentrics
	UINT maxRecursion = Config::cMaxReflectionDepth;
};


class RayTracingPipeline
{
private:
	Microsoft::WRL::ComPtr<ID3D12StateObject> mStateObject;
	RTPipelineSettings mSettings;

	// The SBT Buffer (The most critical resource)
	D3D12Resource mSBTStorage;

	// Offsets into the SBT
	UINT mRayGenSectionSize = 0;
	UINT mMissSectionSize = 0;
	UINT mHitGroupSectionSize = 0;

	// Helper to calculate shader identifier size
	UINT GetShaderIdentifierSize(ID3D12Device5* pDevice);

public:
	void Initialize(ID3D12Device5* pDevice, D3D12RootSignature* globalSig, D3D12RootSignature* localSig, IDxcBlob* shaderBlob, const RTPipelineSettings& settings);

	/**
	 * @brief Builds the SBT linking each mesh to its material textures.
	 * This allows the shader to know "Mesh X uses Texture Y".
	 */
	void BuildSBT(ID3D12Device5* pDevice, const std::initializer_list<std::span<const MeshGpuData>>& meshes);

	void Dispatch(ID3D12GraphicsCommandList4* pCmd, UINT width, UINT height);
};

