#pragma once
#include "pch.h"

/**
 * @brief Constant buffer layout for view-projection data.
 */
struct ConstantBufferData
{
	DirectX::XMMATRIX vpMatrix; ///< View-Projection matrix
};