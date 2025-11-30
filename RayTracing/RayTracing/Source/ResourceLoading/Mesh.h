#pragma once
#include "Vertex.h"
#include "Texture.h"

/**
 * @brief Container for mesh geometry and its material textures.
 */
struct Mesh
{
	/**< Interleaved vertex attributes (position/normal/uv). */
	std::vector<Vertex> mVertices;
	/**< Triangle index buffer. */
	std::vector<unsigned int> mIndices;
	/**< Material-bound textures for this mesh. */
	std::vector<Texture> mTextures;
		/**< Center of the mesh's bounding box. */ // Temporary for sorting before ray tracing
	DirectX::XMFLOAT3 mCenter;
	/**< Indicates if the mesh has any transparency in its textures. */
	bool mIsTransparent;

	/**
	 * @brief Constructs a mesh from given vertex/index/texture data.
	 * @param vertices Vertex array.
	 * @param indices Index array.
	 * @param textures Texture array.
	 * @param center Center of the mesh's bounding box.
	 * @param isTransparent Whether the mesh has transparency.
	 */
	Mesh(const std::vector<Vertex>& vertices,
		const std::vector<unsigned int>& indices,
		const std::vector<Texture>& textures, DirectX::XMFLOAT3 center = {}, bool isTransparent = false)
		: mVertices(vertices), mIndices(indices), mTextures(textures), mCenter(center), mIsTransparent(isTransparent)
	{
	}
};