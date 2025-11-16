#pragma once
struct Vertex
{
	/**< 3D position of the vertex in object space. */
	DirectX::XMFLOAT3 mPosition;
	/**< Normal vector at the vertex. */
	DirectX::XMFLOAT3 mNormal;
	/**< Texture coordinates (UV). */
	DirectX::XMFLOAT2 mTexCoords;
};

