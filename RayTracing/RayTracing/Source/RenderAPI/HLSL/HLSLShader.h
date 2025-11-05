#pragma once

/**
 * @brief Opaque wrapper for a compiled shader blob.
 */
class HLSLShader
{
private:
	Microsoft::WRL::ComPtr<IDxcBlob> mShaderBlob;
public:
	HLSLShader() = default;
	/**
	 * @brief Constructs from an existing blob.
	 */
	HLSLShader(IDxcBlob* shaderBlob) : mShaderBlob(shaderBlob) {}
	/**
	 * @brief Returns the underlying shader blob.
	 */
	IDxcBlob* GetShaderBlob() const { return mShaderBlob.Get(); }
};

