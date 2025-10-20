#include "pch.h"
#include "helpers.h"
#include "HLSLCompiler.h"

using std::wcout, std::wcerr, std::endl;

void HLSLCompiler::Initialize()
{
	HRESULT hr = DxcCreateInstance(
		CLSID_DxcCompiler,
		IID_PPV_ARGS(mCompiler.ReleaseAndGetAddressOf())
	);
	ASSERT_HR(hr, "Failed to create DXC Compiler instance.");

	hr = DxcCreateInstance(
		CLSID_DxcUtils,
		IID_PPV_ARGS(mUtils.ReleaseAndGetAddressOf())
	);
	ASSERT_HR(hr, "Failed to create DXC Utils instance.");

	hr = mUtils->CreateDefaultIncludeHandler(mIncludeHandler.ReleaseAndGetAddressOf());
	ASSERT_HR(hr, "Failed to create default include handler.");
}

HLSLShader HLSLCompiler::CompileFromFile(LPCWSTR filePath, LPCWSTR target, LPCWSTR entryPoint) const
{
	Microsoft::WRL::ComPtr<IDxcBlobEncoding> sourceBlob;
	HRESULT hr = mUtils->LoadFile(
		filePath,
		nullptr,
		sourceBlob.GetAddressOf()
	);

	ASSERT_HR(hr, "Failed to load HLSL file: " << filePath);

	DxcBuffer sourceBuffer{};
	sourceBuffer.Ptr = sourceBlob->GetBufferPointer();
	sourceBuffer.Size = sourceBlob->GetBufferSize();
	sourceBuffer.Encoding = DXC_CP_ACP;

	LPCWSTR arguments[] = {
		L"-E", // entry point
		entryPoint,
		L"-T", // target profile
		target, // e.g., "ps_6_0"
#ifdef _DEBUG
		L"-Zi", // enable debug information
		L"-Od", // disable optimizations
		L"-Qembed_debug", // embed debug info in the shader
#else
		L"-O3", // optimization level 3
#endif
	};

	Microsoft::WRL::ComPtr<IDxcResult> compileResult;
	hr = mCompiler->Compile(
		&sourceBuffer,
		arguments,
		_countof(arguments),
		mIncludeHandler.Get(),
		IID_PPV_ARGS(compileResult.GetAddressOf())
	);

	if (FAILED(hr))
	{
		wcerr << "Failed to compile HLSL shader: " << filePath << endl;
		Microsoft::WRL::ComPtr<IDxcBlobEncoding> errorBlob;
		hr = compileResult->GetErrorBuffer(errorBlob.GetAddressOf());
		if (SUCCEEDED(hr) && errorBlob)
		{
			wcerr << static_cast<const char*>(errorBlob->GetBufferPointer()) << endl;
		}
		throw;
	}

	Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob;
	hr = compileResult->GetOutput(
		DXC_OUT_OBJECT,
		IID_PPV_ARGS(shaderBlob.GetAddressOf()),
		nullptr
	);

	ASSERT_HR(hr, "Failed to get compiled shader object.");

	wcout << "Successfully compiled shader: " << filePath << endl;

	return HLSLShader(shaderBlob.Get());
}

HLSLShader HLSLCompiler::LoadFromCso(LPCWSTR filePath) const
{
	Microsoft::WRL::ComPtr<IDxcBlobEncoding> csoBlob;
	HRESULT hr = mUtils->LoadFile(
		filePath,
		nullptr,
		csoBlob.GetAddressOf()
	);
	ASSERT_HR(hr, "Failed to load CSO file: " << filePath);
	wcout << "Successfully loaded CSO: " << filePath << endl;
	return HLSLShader(csoBlob.Get());
}
