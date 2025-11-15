#pragma once

#include <assimp/scene.h>
#include "Mesh.h"

class Model
{
private:
	void processNode(aiNode* node, const aiScene* scene);
	Mesh processMesh(aiMesh* mesh, const aiScene* scene);
	std::vector<Texture> loadMaterialTextures(aiMaterial* mat, aiTextureType type, const std::string& typeName);

public:
	std::vector<Texture> mLoadedTextures;
	std::vector<Mesh> mMeshes;
	std::string mDirectory;
	void loadModel(const std::string& path);
};

