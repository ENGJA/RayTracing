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

	/**
	 * @brief Constructs a mesh from given vertex/index/texture data.
	 * @param vertices Vertex array.
	 * @param indices Index array.
	 * @param textures Texture array.
	 */
	Mesh(const std::vector<Vertex>& vertices,
		const std::vector<unsigned int>& indices,
		const std::vector<Texture>& textures)
		: mVertices(vertices), mIndices(indices), mTextures(textures)
	{
	}
};