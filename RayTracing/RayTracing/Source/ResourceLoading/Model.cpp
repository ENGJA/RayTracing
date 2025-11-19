#include "pch.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>

#include "Model.h"
using std::string, std::vector, std::cerr, std::endl;

void Model::loadModel(const string& path)
{
	Assimp::Importer importer;
	const aiScene* scene = importer.ReadFile(path, aiProcess_Triangulate | aiProcess_FlipUVs);
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		cerr << "ERROR::ASSIMP:: " << importer.GetErrorString() << endl;
		return;
	}

	mDirectory = path.substr(0, path.find_last_of('\\'));
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
		processNode(node->mChildren[i], scene);
}

Mesh Model::processMesh(aiMesh* mesh, const aiScene* scene)
{
	vector<Vertex> vertices;
	vector<unsigned int> indices;
	vector<Texture> textures;
	for (unsigned int i = 0; i < mesh->mNumVertices; i++)
	{
		Vertex vertex{};
		DirectX::XMFLOAT3 vec3{};
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
			DirectX::XMFLOAT2 vec2{};
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
		loadMaterialTextures(material, aiTextureType_BASE_COLOR, TextureType::Albedo),
		loadMaterialTextures(material, aiTextureType_DIFFUSE, TextureType::Albedo), // fallback if BASE_COLOR not present
		loadMaterialTextures(material, aiTextureType_NORMALS, TextureType::Normal),
		loadMaterialTextures(material, aiTextureType_METALNESS, TextureType::Metalness),
		loadMaterialTextures(material, aiTextureType_DIFFUSE_ROUGHNESS, TextureType::Roughness),
		loadMaterialTextures(material, aiTextureType_EMISSIVE, TextureType::Emissive),
	};

	for (const auto& textureList : loadedTextures)
		textures.insert(textures.end(), textureList.begin(), textureList.end());

	return Mesh(vertices, indices, textures);
}

std::vector<Texture> Model::loadMaterialTextures(aiMaterial* mat, aiTextureType aiType, TextureType type)
{
	vector<Texture> textures;
	for (unsigned int i = 0; i < mat->GetTextureCount(aiType); i++)
	{
		aiString str;
		mat->GetTexture(aiType, i, &str);
		bool skip = false;
		auto it = std::find_if(mLoadedTextures.begin(), mLoadedTextures.end(), [&str] (const Texture& tex)
		{
				return tex.mPath == str.C_Str();
		});

		if (it != mLoadedTextures.end())
			textures.push_back(*it);
		else
		{
			Texture texture(type, std::move(str.C_Str()));
			textures.push_back(texture);
			mLoadedTextures.push_back(texture);
		}
	}
	return textures;
}
