#pragma once
#include "HLSLShader.h"

/**
 * @brief Wrapper for HLSL compilation using DXC.
 */
class HLSLCompiler
{
private:
	Microsoft::WRL::ComPtr<IDxcCompiler3> mCompiler;
	Microsoft::WRL::ComPtr<IDxcUtils> mUtils;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> mIncludeHandler;

public:
	/**
	 * @brief Initializes DXC compiler and utilities.
	 */
	void Initialize();
	/**
	 * @brief Compiles an HLSL source file.
	 * @param filePath Path to HLSL file.
	 * @param target Shader target, e.g. L"vs_6_7".
	 * @param entryPoint Entry point function name, default L"main".
	 * @return Compiled shader blob.
	 */
	HLSLShader CompileFromFile(LPCWSTR filePath, LPCWSTR target, LPCWSTR entryPoint = L"main") const;
	/**
	 * @brief Loads a precompiled shader object (CSO).
	 * @param filePath Path to CSO file.
	 * @return Shader blob wrapper.
	 */
	HLSLShader LoadFromCso(LPCWSTR filePath) const;
};

