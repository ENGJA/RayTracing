#pragma once

#include <assimp/scene.h>
#include "Mesh.h"

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
	 */
	void processNode(aiNode* node, const aiScene* scene);
	/**
	 * @brief Converts an Assimp mesh to our `Mesh` representation.
	 * @param mesh Source Assimp mesh.
	 * @param scene Owning Assimp scene.
	 * @return Built `Mesh` with vertices, indices, and textures.
	 */
	Mesh processMesh(aiMesh* mesh, const aiScene* scene);
	/**
	 * @brief Loads material textures of a given type.
	 * @param mat Assimp material.
	 * @param type Texture type (e.g., aiTextureType_DIFFUSE).
	 * @param typeName String name to tag the texture type.
	 * @return List of loaded textures.
	 */
	std::vector<Texture> loadMaterialTextures(aiMaterial* mat, aiTextureType aiType, TextureType type);

public:
	/**< Cache of already loaded textures to deduplicate by path. */
	std::vector<Texture> mLoadedTextures;
	/**< Meshes contained in the model. */
	std::vector<Mesh> mMeshes;
	/**< Directory of the source model, used to resolve relative textures. */
	std::string mDirectory;
	/**
	 * @brief Loads a model from disk using Assimp.
	 * @param path File path to the model.
	 */
	void loadModel(const std::string& path);
};

