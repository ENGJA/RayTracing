#pragma once
#include <iostream>

/**
 * @brief Helper macros for HRESULT error handling.
 */


#define HANDLE_HR(hr, msg, action) if (FAILED(hr)) { std::wcerr << msg << " Error: " << std::hex << hr << std::endl; action; } ///< Helper macro for HRESULT error handling
#define CHECK_HR(hr, msg) HANDLE_HR(hr, msg, ) ///< Checks HRESULT and logs error message if failed
#define RETURN_FAIL_HR(hr, msg) HANDLE_HR(hr, msg, return false) ///< Checks HRESULT and logs error message and returns false if failed
#define ASSERT_HR(hr, msg) HANDLE_HR(hr, msg, throw) ///< Checks HRESULT and logs error message and throws if failed

//#define ASSERT_HR(hr, msg) if (FAILED(hr)) { std::cerr << msg << " Error: " << std::hex << hr << std::endl; throw; } 