#pragma once
class HLSLShader
{
private:
	Microsoft::WRL::ComPtr<IDxcBlob> mShaderBlob;
public:
	HLSLShader() = default;
	HLSLShader(IDxcBlob* shaderBlob) : mShaderBlob(shaderBlob) {}
	IDxcBlob* GetShaderBlob() const { return mShaderBlob.Get(); }
};

