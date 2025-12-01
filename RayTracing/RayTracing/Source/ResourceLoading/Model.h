#pragma once

#include <assimp/scene.h>
#include "Mesh.h"
#include "RenderAPI/DataTypes.h"

/**
 * @brief CPU-side model loader that builds meshes and textures from Assimp scenes.
 */
class Model
{
private:
	/**
	 * @brief Recursively traverse an Assimp node hierarchy and build meshes.
	 * @param node Current node in the scene graph.
	 * @param scene Owning Assimp scene.
	 * @param parentTransform Accumulated transform from parent nodes (aiMatrix4x4).
	 * @param tangentSpaceHandednessMultiplier  -1 for glTF models, 1 for others. (gltf, glb and blend usually have OpenGL convention textures)
	 */
	void processNode(aiNode* node, const aiScene* scene, const aiMatrix4x4& parentTransform, int tangentSpaceHandednessMultiplier);
	/**
	 * @brief Converts an Assimp mesh to our `Mesh` representation.
	 * @param mesh Source Assimp mesh.
	 * @param scene Owning Assimp scene.
	 * @param transform Transform to apply to vertex positions/normals (world transform).
	 * @param tangentSpaceHandednessMultiplier  -1 for glTF models, 1 for others. (gltf, glb and blend usually have OpenGL convention textures)
	 * @return Built `Mesh` with vertices, indices, and textures.
	 */
	Mesh processMesh(aiMesh* mesh, const aiScene* scene, const aiMatrix4x4& transform, int tangentSpaceHandednessMultiplier);
	/**
	 * @brief Loads material textures of a given type.
	 * @param mat Assimp material.
	 * @param type Texture type (e.g., aiTextureType_DIFFUSE).
	 * @param typeName String name to tag the texture type.
	 * @return List of loaded textures.
	 */
	std::vector<Texture> loadMaterialTextures(aiMaterial* mat, aiTextureType aiType, TextureType type);

	/**
	 * @brief Resolves a raw texture path to an absolute path based on the model directory.
	 * @param rawPath Raw texture path from the material.
	 * @return Resolved absolute texture path.
	 */
	std::string ResolveTexturePath(const std::string& rawPath);

public:
	/**< Cache of already loaded textures to deduplicate by path. */
	std::vector<Texture> mLoadedTextures;
	/**< Meshes contained in the model. */
	std::vector<Mesh> mMeshes;
	/**< Lights contained in the model. */
	std::vector<LightData> mLights;
	/**< Directory of the source model, used to resolve relative textures. */
	std::string mDirectory;
	/**
	 * @brief Extracts alpha properties from a material (render layer and cutoff).
	 * @param material Assimp material.
	 */
	AlphaProperties GetAlphaProperties(const aiMaterial* material);
	/**
	 * @brief Loads a model from disk using Assimp.
	 * @param path File path to the model.
	 */
	void loadModel(const std::string& path);
};

