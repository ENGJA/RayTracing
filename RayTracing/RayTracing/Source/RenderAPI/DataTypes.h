#pragma once
#include "pch.h"

static constexpr int cMaxLights = 25;

struct LightData
{
	DirectX::XMFLOAT4 position;
	//DirectX::XMFLOAT4 color;
	DirectX::XMFLOAT4 dirType;
	DirectX::XMFLOAT4 diffuseColor;
	DirectX::XMFLOAT4 specularColor;
};

/**
 * @brief Constant buffer layout for view-projection data.
 */
struct ConstantBufferData
{
	DirectX::XMMATRIX vpMatrix;		///< View-Projection matrix
	DirectX::XMMATRIX InvVpMatrix;    // Compute Shader needs this (NEW)
	DirectX::XMFLOAT3 viewPos;		///< world-space camera position (w .xyz)
	int numLights;					///< number of active lights

	int frameCount;
	int _pad[3];
	LightData lights[cMaxLights];
};

struct MeshMaterialData
{
	DirectX::XMFLOAT4 baseColorFactor = { 1.0f, 1.0f, 1.0f, 1.0f };
	float metalnessFactor = 1.0f;
	float roughnessFactor = 1.0f;
	float alphaCutoff = 0.5f;
	float _pad[1]; // Padding for 16-byte alignment
	DirectX::XMFLOAT4 emissiveFactor = { 0.0f, 0.0f, 0.0f, 1.0f }; // .w not used, reserved for alignment
};