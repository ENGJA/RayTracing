#pragma once
/**
 * @brief Root signature helper.
 */
class D3D12RootSignature
{
private:
	Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;

public:
	/**
	 * @brief Creates a default root signature suitable for the sample.
	 * @param pDevice D3D12 device.
	 */
	void Initialize(ID3D12Device* pDevice);// , const D3D12_ROOT_SIGNATURE_DESC& rootSignatureDesc);
	/**
	 * @brief Returns the native root signature pointer.
	 */
	ID3D12RootSignature* Get() const { return mRootSignature.Get(); }
};

