#pragma once
#include "pch.h"

static constexpr int cMaxLights = 25;

struct LightData
{
	DirectX::XMFLOAT4 position;
	DirectX::XMFLOAT4 color;
	DirectX::XMFLOAT4 dirType;
};

/**
 * @brief Constant buffer layout for view-projection data.
 */
struct ConstantBufferData
{
	DirectX::XMMATRIX vpMatrix;		///< View-Projection matrix
	DirectX::XMFLOAT4 viewPos;		///< world-space camera position (w .xyz)
	int numLights;					///< liczba aktywnych œwiate³
	float _pad[3];					///< wyrównanie
	LightData lights[cMaxLights];
};