#include "pch.h"
#include "D3D12RootSignature.h"
#include "RenderAPI/DataTypes.h"
#include "helpers.h"
#include "Config.h"

using std::wcerr, std::endl;
void D3D12RootSignature::InitializeMeshRS(ID3D12Device* pDevice)
{
	// --- 1. Define Ranges ---
	// Range for the 5 material textures (t0, t1, t2, t3, t4)
	CD3DX12_DESCRIPTOR_RANGE1 meshSrvRange;
	meshSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_STATIC);

	// --- 2. Define Parameters ---
	CD3DX12_ROOT_PARAMETER1 meshParams[3];

	// Parameter 0: CBV for Frame Data (b0)
	meshParams[0].InitAsConstantBufferView(0, 0, D3D12_ROOT_DESCRIPTOR_FLAG_DATA_STATIC, D3D12_SHADER_VISIBILITY_ALL);

	// Parameter 1: Descriptor Table for Material Textures (t0-t4)
	meshParams[1].InitAsDescriptorTable(1, &meshSrvRange, D3D12_SHADER_VISIBILITY_PIXEL);

	// Parameter 2: 32-bit Constants for Material Data (b1)
	// Size = sizeof(MeshMaterialData) / 4. Assuming struct is 6 floats + pads.
	meshParams[2].InitAsConstants(sizeof(MeshMaterialData) / 4, 1, 0, D3D12_SHADER_VISIBILITY_PIXEL);

	// --- 3. Define Static Sampler (s0) ---
	CD3DX12_STATIC_SAMPLER_DESC staticSampler(
		0, // ShaderRegister
		D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		0.0f,
		16,
		D3D12_COMPARISON_FUNC_LESS_EQUAL,
		D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE,
		0.0f,
		D3D12_FLOAT32_MAX,
		D3D12_SHADER_VISIBILITY_PIXEL
	);

	// --- 4. Create the Description ---
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC meshSigDesc;
	meshSigDesc.Init_1_1(
		_countof(meshParams),
		meshParams,
		1,
		&staticSampler,
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT // <--- Critical for Vertex Shaders
	);

	Initialize(pDevice, meshSigDesc);
}

void D3D12RootSignature::InitializeComputeRS(ID3D12Device* pDevice)
{
	// --- 1. Define Ranges ---
	// Range 1: G-Buffer Inputs (Albedo, Normal, Material, Depth, Emissive) -> t0-t4
	CD3DX12_DESCRIPTOR_RANGE1 gbufferSrvRange;
	gbufferSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);

	// Range 2: Output Texture (UAV) -> u0
	CD3DX12_DESCRIPTOR_RANGE1 outputUavRange;
	outputUavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);

	// --- 2. Define Parameters ---
	CD3DX12_ROOT_PARAMETER1 computeParams[5]{};

	// Parameter 0: CBV for Frame Data (b0)
	computeParams[0].InitAsConstantBufferView(0);

	// Parameter 1: Descriptor Table for G-Buffer (t0-t4)
	computeParams[1].InitAsDescriptorTable(1, &gbufferSrvRange);

	// Parameter 2: Root SRV for TLAS (t5)
	// Using Root SRV (SetComputeRootShaderResourceView) is faster/cleaner for TLAS than a table
	computeParams[2].InitAsShaderResourceView(5);

	// Parameter 3: Root SRV for Light Buffer (t6)
	computeParams[3].InitAsShaderResourceView(6);

	// Parameter 4: Descriptor Table for Output UAV (u0)
	computeParams[4].InitAsDescriptorTable(1, &outputUavRange);

	// --- 3. Create the Description ---
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC computeSigDesc;
	computeSigDesc.Init_1_1(
		_countof(computeParams),
		computeParams,
		0,
		nullptr, // No samplers usually needed for G-Buffer read (Load() doesn't sample)
		D3D12_ROOT_SIGNATURE_FLAG_NONE // Compute shaders don't use Input Assembler
	);

	Initialize(pDevice, computeSigDesc);
}

void D3D12RootSignature::InitializeCompositeRS(ID3D12Device* pDevice)
{
	CD3DX12_DESCRIPTOR_RANGE1 ranges[5]{};
	// Range 0: Diffuse, specular, albedo, albedoSpecular SRVs (t0-t3) - 4 Descriptors
	ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 4, 0);
	// Range 1: Normals SRV (t4) - 1 Descriptor
	ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4);
	// Range 2: Depth SRV (t5) - 1 Descriptor
	ranges[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5);
	// Range 3: Emissive SRV (t6) - 1 Descriptor
	ranges[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6);
	// Range 4: Output UAV (u0) - 1 Descriptor
	ranges[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

	CD3DX12_ROOT_PARAMETER1 params[6]{};
	// Param 0: Constant Buffer (b0)
	params[0].InitAsConstantBufferView(0);
	// Param 1: Direct Lighting SRV (t0)
	params[1].InitAsDescriptorTable(1, &ranges[0]);
	// Param 2: G-Buffer Normal SRV (t4)
	params[2].InitAsDescriptorTable(1, &ranges[1]);
	// Param 3: G-Buffer Depth SRV (t5)
	params[3].InitAsDescriptorTable(1, &ranges[2]);
	// Param 4: G-Buffer Emissive SRV (t6)
	params[4].InitAsDescriptorTable(1, &ranges[3]);
	// Param 5: Output UAV (u0)
	params[5].InitAsDescriptorTable(1, &ranges[4]);

	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC sigDesc{};
	sigDesc.Init_1_1(_countof(params), params);

	Initialize(pDevice, sigDesc);
}

void D3D12RootSignature::InitializeTonemapRS(ID3D12Device* pDevice)
{
	CD3DX12_DESCRIPTOR_RANGE1 ranges[2]{};
	// Range 0: Input HDR Texture (t0) - 1 Descriptor
	ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
	// Range 1: Output LDR Texture (u0) - 1 Descriptor
	ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);
	CD3DX12_ROOT_PARAMETER1 params[2]{};
	// Param 0: Input HDR SRV (t0)
	params[0].InitAsDescriptorTable(1, &ranges[0]);
	// Param 1: Output LDR UAV (u0)
	params[1].InitAsDescriptorTable(1, &ranges[1]);
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC sigDesc{};
	sigDesc.Init_1_1(_countof(params), params);
	Initialize(pDevice, sigDesc);
}

void D3D12RootSignature::Initialize(ID3D12Device* pDevice, const D3D12_VERSIONED_ROOT_SIGNATURE_DESC& rootSignatureDesc)
{
	Microsoft::WRL::ComPtr<ID3DBlob> serializedRootSig;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;
	HRESULT hr = D3D12SerializeVersionedRootSignature(
		&rootSignatureDesc,
		serializedRootSig.GetAddressOf(),
		errorBlob.GetAddressOf()
	);

	if (FAILED(hr))
	{
		wcerr << "Failed to serialize root signature. Error: " << std::hex << hr << endl;
		if (errorBlob)
			wcerr << static_cast<const char*>(errorBlob->GetBufferPointer()) << endl;
		throw;
	}

	hr = pDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.ReleaseAndGetAddressOf())
	);

	ASSERT_HR(hr, "Failed to create root signature.");
}

void D3D12RootSignature::InitializeRTGlobalRS(ID3D12Device* pDevice)
{
	// --- 1. Define Ranges ---
	// Range 1: G-Buffer Inputs (Albedo, Normal, Material, Depth, Emissive) -> t0-t4
	CD3DX12_DESCRIPTOR_RANGE1 gbufferSrvRange;
	gbufferSrvRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 5, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);

	// Range 2: Output Texture (UAV) -> u0-u3
	CD3DX12_DESCRIPTOR_RANGE1 outputUavRange;
	outputUavRange.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 4, 0, 0, D3D12_DESCRIPTOR_RANGE_FLAG_DATA_VOLATILE);

	// --- 2. Define Parameters ---
	CD3DX12_ROOT_PARAMETER1 computeParams[5]{};

	// Parameter 0: CBV for Frame Data (b0)
	computeParams[0].InitAsConstantBufferView(0);

	// Parameter 1: Descriptor Table for G-Buffer (t0-t4)
	computeParams[1].InitAsDescriptorTable(1, &gbufferSrvRange);

	// Parameter 2: Root SRV for TLAS (t5)
	// Using Root SRV (SetComputeRootShaderResourceView) is faster/cleaner for TLAS than a table
	computeParams[2].InitAsShaderResourceView(5);

	// Parameter 3: Root SRV for Light Buffer (t6)
	computeParams[3].InitAsShaderResourceView(6);

	// Parameter 4: Descriptor Table for Output UAV (u0)
	computeParams[4].InitAsDescriptorTable(1, &outputUavRange);

	// --- 3. Create the Description ---
	CD3DX12_VERSIONED_ROOT_SIGNATURE_DESC rtSigDesc;
	rtSigDesc.Init_1_1(
		_countof(computeParams),
		computeParams,
		0,
		nullptr, // No samplers usually needed for G-Buffer read (Load() doesn't sample)
		D3D12_ROOT_SIGNATURE_FLAG_NONE // Compute shaders don't use Input Assembler
	);

	Initialize(pDevice, rtSigDesc);
}

