#pragma once
#include "HLSLShader.h"
class HLSLCompiler
{
private:
	Microsoft::WRL::ComPtr<IDxcCompiler3> mCompiler;
	Microsoft::WRL::ComPtr<IDxcUtils> mUtils;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> mIncludeHandler;

public:
	void Initialize();
	HLSLShader CompileFromFile(LPCWSTR filePath, LPCWSTR target, LPCWSTR entryPoint = L"main");
};

