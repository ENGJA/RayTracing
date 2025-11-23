#pragma once

/**
* @brief Encapsulates a root signature for mipmap generation.
*/
class MipmapRootSignature
{
private:
	Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature; ///< Root signature COM pointer.

public:
	/**
	 * @brief Creates a default root signature suitable for mipmap generation.
	 * @param pDevice D3D12 device.
	 */
	void Initialize(ID3D12Device* pDevice);

	/**
	 * @brief Returns the native root signature pointer.
	 */
	ID3D12RootSignature* Get() const { return mRootSignature.Get(); }
};

