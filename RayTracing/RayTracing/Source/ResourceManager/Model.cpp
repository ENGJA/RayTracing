#include "assimp/Importer.hpp"
#include "assimp/postprocess.h"

#include "Model.h"
#include "pch.h"
#include "Model.h"
using namespace std;

void Model::loadModel(const string& path)
{
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_FlipUVs);
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		cerr << "ERROR::ASSIMP:: " << importer.GetErrorString() << endl;
		return;
	}

	mDirectory = path.substr(0, path.find_last_of('/'));
	processNode(scene->mRootNode, scene);
}

void Model::processNode(aiNode* node, const aiScene* scene)
{
	for (unsigned int i = 0; i < node->mNumMeshes; i++)
	{
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
		mMeshes.push_back(processMesh(mesh, scene));
	}
	for (unsigned int i = 0; i < node->mNumChildren; i++)
	{
		processNode(node->mChildren[i], scene);
	}
}

Mesh Model::processMesh(aiMesh* mesh, const aiScene* scene)
{
	vector<Vertex> vertices;
	vector<unsigned int> indices;
	vector<Texture> textures;
	for (unsigned int i = 0; i < mesh->mNumVertices; i++)
	{
		Vertex vertex;
		DirectX::XMFLOAT3 vec3;
		vec3.x = mesh->mVertices[i].x;
		vec3.y = mesh->mVertices[i].y;
		vec3.z = mesh->mVertices[i].z;
		vertex.mPosition = vec3;
		if (mesh->mNormals)
		{
			vec3.x = mesh->mNormals[i].x;
			vec3.y = mesh->mNormals[i].y;
			vec3.z = mesh->mNormals[i].z;
			vertex.mNormal = vec3;
		}
		if (mesh->mTextureCoords[0])
		{
			DirectX::XMFLOAT2 vec2;
			vec2.x = mesh->mTextureCoords[0][i].x;
			vec2.y = mesh->mTextureCoords[0][i].y;
			vertex.mTexCoords = vec2;
		}
		else
		{
			vertex.mTexCoords = DirectX::XMFLOAT2(0.0f, 0.0f);
		}
		vertices.push_back(vertex);
	}
	for (unsigned int i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];
		for (unsigned int j = 0; j < face.mNumIndices; j++)
			indices.push_back(face.mIndices[j]);
	}

	// get PBR material textures
	aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
	vector<vector<Texture>> loadedTextures =
	{
		loadMaterialTextures(material, aiTextureType_BASE_COLOR, "texture_albedo"),
		loadMaterialTextures(material, aiTextureType_NORMALS, "texture_normal"),
		loadMaterialTextures(material, aiTextureType_METALNESS, "texture_metalness"),
		loadMaterialTextures(material, aiTextureType_DIFFUSE_ROUGHNESS, "texture_roughness"),
		loadMaterialTextures(material, aiTextureType_EMISSIVE, "texture_emissive"),
	};
	for (const auto& textureList : loadedTextures)
	{
		textures.insert(textures.end(), textureList.begin(), textureList.end());
	}
}

std::vector<Texture> Model::loadMaterialTextures(aiMaterial* mat, aiTextureType type, const std::string& typeName)
{
	vector<Texture> textures;
	for (unsigned int i = 0; i < mat->GetTextureCount(type); i++)
	{
		aiString str;
		mat->GetTexture(type, i, &str);
		bool skip = false;
		for (unsigned int j = 0; j < mLoadedTextures.size(); j++)
		{
			if (std::strcmp(mLoadedTextures[j].mPath.c_str(), str.C_Str()) == 0)
			{
				textures.push_back(mLoadedTextures[j]);
				skip = true;
				break;
			}
		}
		if (!skip)
		{
			Texture texture;
			texture.mType = typeName;
			texture.mPath = str.C_Str();
			textures.push_back(texture);
			mLoadedTextures.push_back(texture);
		}
	}
}
