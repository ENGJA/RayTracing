#include "pch.h"
#include "helpers.h"

using Microsoft::WRL::ComPtr, std::wcerr, std::endl;
void CreateRootSignature(ID3D12Device* pDevice, const D3D12_VERSIONED_ROOT_SIGNATURE_DESC& rootSignatureDesc, ComPtr<ID3D12RootSignature>& mRootSignature)
{
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
