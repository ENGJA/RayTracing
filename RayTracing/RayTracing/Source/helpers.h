#pragma once
#include <iostream>
#include <filesystem>

/**
 * @brief Helper macros for HRESULT error handling and logging.
 */


/// Checks HRESULT and logs error message if failed
#define HANDLE_HR(hr, msg, action) if (FAILED(hr)) { std::wcerr << msg << " Error: " << std::hex << hr << std::endl; action; }
/// Checks HRESULT and logs error message if failed
#define CHECK_HR(hr, msg) HANDLE_HR(hr, msg, )
/// Checks HRESULT, logs error, and returns false if failed
#define RETURN_FAIL_HR(hr, msg) HANDLE_HR(hr, msg, return false)
/// Checks HRESULT, logs error, and throws if failed
#define ASSERT_HR(hr, msg) HANDLE_HR(hr, msg, throw)

//#define ASSERT_HR(hr, msg) if (FAILED(hr)) { std::cerr << msg << " Error: " << std::hex << hr << std::endl; throw; } 

void CreateRootSignature(ID3D12Device * pDevice, const D3D12_VERSIONED_ROOT_SIGNATURE_DESC& rootSignatureDesc, Microsoft::WRL::ComPtr<ID3D12RootSignature>&mRootSignature);


