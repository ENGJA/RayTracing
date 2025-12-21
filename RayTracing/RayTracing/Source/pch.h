#pragma once
#define NOMINMAX

// standard library
#include <iostream>
#include <vector>
#include <unordered_map>
#include <filesystem>
#include <cmath>
#include <functional>
#include <future>
#include <chrono>
#include <string>
#include <memory>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <ranges>

// directx
#include <d3d12.h>
#include <DirectXMath.h>
#include <dxcapi.h>
#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <d3dx12.h>
#include <DDSTextureLoader.h>
#include <ResourceUploadBatch.h>

// nvidia
#include <streamline/sl.h>
#include <streamline/sl_consts.h>
#include <streamline/sl_dlss.h> // Includes Ray Reconstruction

// windows
#include <windows.h>
#include <wrl.h>
#include <wincodec.h>
#include <commdlg.h> 

//#pragma comment(lib, "windowscodecs.lib")