#pragma once
#include <iostream>

#define HANDLE_HR(hr, msg, action) if (FAILED(hr)) { std::cerr << msg << " Error: " << std::hex << hr << std::endl; action; }
#define CHECK_HR(hr, msg) HANDLE_HR(hr, msg, )
#define RETURN_FAIL_HR(hr, msg) HANDLE_HR(hr, msg, return false)
#define ASSERT_HR(hr, msg) HANDLE_HR(hr, msg, throw)
//#define ASSERT_HR(hr, msg) if (FAILED(hr)) { std::cerr << msg << " Error: " << std::hex << hr << std::endl; throw; } 