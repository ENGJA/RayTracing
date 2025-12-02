#pragma once
#include "Vertex.h"
#include "Texture.h"
#include "RenderAPI/DataTypes.h"

enum class RenderLayer
{
	Opaque,		// Solid objects
	Masked,		// Cutout transparency (e.g., foliage)
	Blend	// Semi-transparent objects (like glass)
};

struct AlphaProperties
{
	RenderLayer mRenderLayer = RenderLayer::Opaque;
	float alphaCutoff = 0.5f;
};

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
	/**< Material properties for this mesh. */
	MeshMaterialData mMaterialData;
	/**< Render layer based on alpha properties. */
	RenderLayer mRenderLayer;
	/**< Whether the mesh is double-sided. */
	bool doubleSided = false;


	/**
	 * @brief Constructs a mesh from given vertex/index/texture data.
	 * @param vertices Vertex array.
	 * @param indices Index array.
	 * @param textures Texture array.
	 * @param center Center of the mesh's bounding box.
	 * @param materialData Material properties for the mesh.
	 * @param renderLayer Render layer based on alpha properties.
	 */
	Mesh(const std::vector<Vertex>& vertices,
		const std::vector<unsigned int>& indices,
		const std::vector<Texture>& textures, DirectX::XMFLOAT3 center = {}, const MeshMaterialData& materialData = {}, RenderLayer renderLayer = RenderLayer::Opaque)
		: mVertices(vertices), mIndices(indices), mTextures(textures), mCenter(center), mMaterialData(materialData), mRenderLayer(renderLayer)
	{
	}
};