#pragma once
#include "Vertex.h"
#include "Texture.h"

struct Mesh
{
	std::vector<Vertex> mVertices;
	std::vector<unsigned int> mIndices;
	std::vector<Texture> mTextures;

	Mesh(const std::vector<Vertex>& vertices,
		const std::vector<unsigned int>& indices,
		const std::vector<Texture>& textures)
		: mVertices(vertices), mIndices(indices), mTextures(textures)
	{
	}
};