#pragma once
/**
 * @brief Root signature helper.
 */
class D3D12RootSignature
{
private:
	Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature; ///< Root signature COM pointer.

public:
	/**
	 * @brief Creates a default root signature suitable for the sample.
	 * @param pDevice D3D12 device.
	 */
	void InitializeMeshRS(ID3D12Device* pDevice);// , const D3D12_ROOT_SIGNATURE_DESC& rootSignatureDesc);

	void InitializeComputeRS(ID3D12Device* pDevice);

	void InitializeCompositeRS(ID3D12Device* pDevice);

	void InitializeTonemapRS(ID3D12Device* pDevice);

	void Initialize(ID3D12Device* pDevice, const D3D12_VERSIONED_ROOT_SIGNATURE_DESC& rootSignatureDesc);

	void InitializeRTGlobalRS(ID3D12Device* pDevice);

	/**
	 * @brief Returns the native root signature pointer.
	 */
	ID3D12RootSignature* Get() const { return mRootSignature.Get(); }
};

