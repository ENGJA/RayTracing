#pragma once
#include <iostream>
#define ASSERT_HR(hr, msg) if (FAILED(hr)) { std::cerr << msg << " Error: " << std::hex << hr << std::endl; throw; } 