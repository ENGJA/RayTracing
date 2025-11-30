#pragma once
struct Vertex
{
	/**< 3D position of the vertex in object space. */
	DirectX::XMFLOAT3 mPosition;
	/**< Normal vector at the vertex. */
	DirectX::XMFLOAT3 mNormal;
	/**< Texture coordinates (UV). */
	DirectX::XMFLOAT2 mTexCoords;
	/**< Tangent vector at the vertex (xyz = direction, w = handedness for bitangent) */
	DirectX::XMFLOAT4 mTangent;
	/**< Per-vertex material props packed: x = metalness, y = shininess (specular exponent). */
	DirectX::XMFLOAT2 mMaterialProps{ 0.0f, 32.0f };
};

