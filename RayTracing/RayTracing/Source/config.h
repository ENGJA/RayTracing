#pragma once
#include <wtypes.h>

/**
 * @brief Global configuration constants.
 */
namespace Config
{
	constexpr static UINT cBufferCount = 2; ///< Back buffer count
	constexpr static UINT cFrameCount = cBufferCount; ///< Frame count (same as buffer count)
	constexpr static DXGI_FORMAT cBackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM; ///< Back buffer format
	constexpr static DXGI_FORMAT cDepthBufferFormat = DXGI_FORMAT_D32_FLOAT; ///< Depth buffer format
	constexpr static UINT cNumberOfTextureSlots = 5; ///< Number of texture slots per material
	constexpr static UINT cNumberOfSrvDescriptors = 4096;
}