#include "pch.h"
#include "D3D12RootSignature.h"
#include "RenderAPI/DataTypes.h"
#include "helpers.h"

using std::wcerr, std::endl;
void D3D12RootSignature::Initialize(ID3D12Device* pDevice)
{
	// Root parameters:
	// 0: CBV b0
	// 1: Descriptor table with 5 SRVs (t0 - t4) for material textures
	// 2: 32-bit constants b1 for MeshMaterialData
	D3D12_DESCRIPTOR_RANGE1 srvRange{};
	srvRange.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	srvRange.NumDescriptors = 5; // baseColor, normal, metalness, roughness, emissive
	srvRange.BaseShaderRegister = 0; // t0
	srvRange.RegisterSpace = 0;
	srvRange.Flags = D3D12_DESCRIPTOR_RANGE_FLAG_NONE;
	srvRange.OffsetInDescriptorsFromTableStart = 0;

	D3D12_ROOT_PARAMETER1 rootParameters[3] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[0].Descriptor.ShaderRegister = 0;
	rootParameters[0].Descriptor.RegisterSpace = 0;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[1].DescriptorTable.NumDescriptorRanges = 1;
	rootParameters[1].DescriptorTable.pDescriptorRanges = &srvRange;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;


	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
	rootParameters[2].Constants.ShaderRegister = 1; // b1
	rootParameters[2].Constants.RegisterSpace = 0;
	rootParameters[2].Constants.Num32BitValues = sizeof(MeshMaterialData) / 4;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	// Static sampler at s0 (use version 1.2 structure)
	D3D12_STATIC_SAMPLER_DESC staticSampler{};
	staticSampler.Filter = D3D12_FILTER_ANISOTROPIC;
	staticSampler.AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSampler.AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSampler.AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSampler.MipLODBias = 0.0f;
	staticSampler.MaxAnisotropy = 16;
	staticSampler.ComparisonFunc = D3D12_COMPARISON_FUNC_ALWAYS;
	staticSampler.BorderColor = D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE;
	staticSampler.MinLOD = 0.0f;
	staticSampler.MaxLOD = D3D12_FLOAT32_MAX;
	staticSampler.ShaderRegister = 0; // s0
	staticSampler.RegisterSpace = 0;
	staticSampler.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc{};
	rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
	rootSignatureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	rootSignatureDesc.Desc_1_1.NumParameters = _countof(rootParameters);
	rootSignatureDesc.Desc_1_1.pParameters = rootParameters;
	rootSignatureDesc.Desc_1_1.NumStaticSamplers = 1;
	rootSignatureDesc.Desc_1_1.pStaticSamplers = &staticSampler;

	CreateRootSignature(pDevice, rootSignatureDesc, mRootSignature);
}

void D3D12RootSignature::InitializeCompute(ID3D12Device* pDevice)
{
	// Denoise Root Sig: 
	CD3DX12_ROOT_PARAMETER1 rootParameters[2] = {};

	// b0 - blend factor`
	rootParameters[0].InitAsConstants(1, 0);

	// 2. Descriptor Table (t0, t1, u0)
	CD3DX12_DESCRIPTOR_RANGE1 ranges[2]{};
	// Range 0: SRVs (t0, t1, t2) -> NumDescriptors = 3
	ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 3, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);

	// Range 1: UAV (u0) -> NumDescriptors = 1
	ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DESCRIPTORS_VOLATILE);

	rootParameters[1].InitAsDescriptorTable(2, ranges);

	CD3DX12_STATIC_SAMPLER_DESC staticSampler(
		0, // ShaderRegister (s0)
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP
	);

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc;
	rootSignatureDesc.Init_1_1(
		_countof(rootParameters),
		rootParameters,
		1,
		&staticSampler,
		D3D12_ROOT_SIGNATURE_FLAG_NONE
	);

	CreateRootSignature(pDevice, rootSignatureDesc, mRootSignature);
}
