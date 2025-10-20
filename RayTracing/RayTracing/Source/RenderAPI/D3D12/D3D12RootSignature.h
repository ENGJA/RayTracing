#pragma once
class D3D12RootSignature
{
private:
	Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;

public:
	void Initialize(ID3D12Device* pDevice);// , const D3D12_ROOT_SIGNATURE_DESC& rootSignatureDesc);
};

