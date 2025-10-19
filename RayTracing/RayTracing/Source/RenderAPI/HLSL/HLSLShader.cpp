#include "pch.h"
//#include "helpers.h"
#include "HLSLShader.h"

//using std::wcout, std::endl;
//void HLSLShader::LoadFromFile(HLSLCompiler& compiler, LPCWSTR filePath, LPCSTR target, LPCSTR entryPoint)
//{
//	UINT flags = D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_ALL_RESOURCES_BOUND;
//#ifdef _DEBUG
//	flags |= D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
//#else
//	flags |= D3DCOMPILE_OPTIMIZATION_LEVEL3;
//#endif
//
//	Microsoft::WRL::ComPtr<ID3DBlob> mErrorBlob;
//
//	HRESULT hr = D3DCompileFromFile(
//		filePath,
//		nullptr,
//		D3D_COMPILE_STANDARD_FILE_INCLUDE,
//		entryPoint,
//		target,
//		flags,
//		0,
//		mShaderBlob.ReleaseAndGetAddressOf(),
//		mErrorBlob.GetAddressOf()
//	);
//
//
//	ASSERT_HR(hr, "Failed to compile HLSL shader." << (mErrorBlob ? static_cast<const char*>(mErrorBlob->GetBufferPointer()) : ""));
//
//	wcout << "Successfully compiled shader: " << filePath << endl;
//}
