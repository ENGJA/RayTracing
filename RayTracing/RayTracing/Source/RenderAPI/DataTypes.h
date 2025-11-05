#pragma once
#include "pch.h"

/**
 * @brief Vertex format used by the sample.
 */
struct Vertex
{
	DirectX::XMFLOAT3 position; ///< Position in object space
	DirectX::XMFLOAT4 color;    ///< Vertex color
};

/**
 * @brief Constant buffer layout for view-projection data.
 */
struct ConstantBufferData
{
	DirectX::XMMATRIX vpMatrix; ///< View-Projection matrix
};