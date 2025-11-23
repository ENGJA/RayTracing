#include "pch.h"
#include "MipmapRootSignature.h"
#include "helpers.h"

using Microsoft::WRL::ComPtr, std::wcerr, std::endl;

void MipmapRootSignature::Initialize(ID3D12Device* pDevice)
{
    // Descriptor table with SRV for source texture and UAV for destination texture
    D3D12_DESCRIPTOR_RANGE1 ranges[2] = {};
    ranges[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    ranges[0].NumDescriptors = 1;
    ranges[0].BaseShaderRegister = 0;
    ranges[0].RegisterSpace = 0;
    ranges[0].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    ranges[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    ranges[1].NumDescriptors = 1;
    ranges[1].BaseShaderRegister = 0;
    ranges[1].RegisterSpace = 0;
    ranges[1].OffsetInDescriptorsFromTableStart = D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

    D3D12_ROOT_PARAMETER1 params[3] = {};

    // SRV
    params[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[0].DescriptorTable.NumDescriptorRanges = 1;
    params[0].DescriptorTable.pDescriptorRanges = &ranges[0];
    params[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // UAV
    params[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    params[1].DescriptorTable.NumDescriptorRanges = 1;
    params[1].DescriptorTable.pDescriptorRanges = &ranges[1];
    params[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

    // Constants
    params[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;
    params[2].Constants.Num32BitValues = 5;
    params[2].Constants.ShaderRegister = 0;
    params[2].Constants.RegisterSpace = 0;
    params[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;

	D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc{};
	rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_1;
	rootSignatureDesc.Desc_1_1.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	rootSignatureDesc.Desc_1_1.NumParameters = _countof(params);
	rootSignatureDesc.Desc_1_1.pParameters = params;

	ComPtr<ID3DBlob> serializedRootSig;
	ComPtr<ID3DBlob> errorBlob;
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
