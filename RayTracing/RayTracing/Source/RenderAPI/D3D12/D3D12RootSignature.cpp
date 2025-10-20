#include "pch.h"
#include "D3D12RootSignature.h"
#include "helpers.h"

using std::wcerr, std::endl;
void D3D12RootSignature::Initialize(ID3D12Device* pDevice)
{
	D3D12_ROOT_PARAMETER1 rootParameters[1] = {};
	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[0].Descriptor.ShaderRegister = 0;
	rootParameters[0].Descriptor.RegisterSpace = 0;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;


	D3D12_VERSIONED_ROOT_SIGNATURE_DESC rootSignatureDesc{};
	rootSignatureDesc.Version = D3D_ROOT_SIGNATURE_VERSION_1_2;
	rootSignatureDesc.Desc_1_2.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
	rootSignatureDesc.Desc_1_2.NumParameters = 1;
	rootSignatureDesc.Desc_1_2.pParameters = rootParameters;


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