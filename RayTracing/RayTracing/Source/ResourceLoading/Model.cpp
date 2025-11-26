#include "pch.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>

#include "Model.h"
#include <RenderAPI/DataTypes.h>
using std::string, std::vector, std::cerr, std::endl;

static DirectX::XMMATRIX AiToXMMatrix(const aiMatrix4x4& m)
{
	return DirectX::XMMATRIX(
		(float)m.a1, (float)m.a2, (float)m.a3, (float)m.a4,
		(float)m.b1, (float)m.b2, (float)m.b3, (float)m.b4,
		(float)m.c1, (float)m.c2, (float)m.c3, (float)m.c4,
		(float)m.d1, (float)m.d2, (float)m.d3, (float)m.d4
	);
}

void Model::loadModel(const string& path)
{
	Assimp::Importer importer;
	const unsigned int flags =
		aiProcess_Triangulate |
		aiProcess_FlipUVs |
		aiProcess_GenSmoothNormals |
		aiProcess_CalcTangentSpace |
		aiProcess_JoinIdenticalVertices |
		aiProcess_PreTransformVertices;
	const aiScene* scene = importer.ReadFile(path, flags);
	if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode)
	{
		cerr << "ERROR::ASSIMP:: " << importer.GetErrorString() << endl;
		return;
	}

	try
	{
		std::filesystem::path p(path);
		mDirectory = p.parent_path().string();
	}
	catch (...)
	{
		mDirectory = path.substr(0, path.find_last_of('\\'));
	}
	aiMatrix4x4 identity;
	processNode(scene->mRootNode, scene, identity);
}

void Model::processNode(aiNode* node, const aiScene* scene, const aiMatrix4x4& parentTransform)
{
	aiMatrix4x4 currentTransform = parentTransform * node->mTransformation;

	for (unsigned int i = 0; i < node->mNumMeshes; i++)
	{
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];

		aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];
		const float alphaThreshold = 0.999f; 

		aiColor4D diffuseColor;
		if (AI_SUCCESS == aiGetMaterialColor(mat, AI_MATKEY_COLOR_DIFFUSE, &diffuseColor))
		{
			if (diffuseColor.a < alphaThreshold)
				continue;
		}

		mMeshes.push_back(processMesh(mesh, scene, currentTransform));
	}
	for (unsigned int i = 0; i < node->mNumChildren; i++)
		processNode(node->mChildren[i], scene, currentTransform);
}

Mesh Model::processMesh(aiMesh* mesh, const aiScene* scene, const aiMatrix4x4& transform)
{
	vector<Vertex> vertices;
	vector<unsigned int> indices;
	vector<Texture> textures;

	DirectX::XMMATRIX xmTransform = AiToXMMatrix(transform);

	aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
	float matMetalness = 0.0f;
	float matShininess = 32.0f;

	if (material)
	{
		ai_real mf = 0.0;
		if (AI_SUCCESS == aiGetMaterialFloat(material, AI_MATKEY_METALLIC_FACTOR, &mf))
			matMetalness = static_cast<float>(mf);

		ai_real sf = 0.0;
		if (AI_SUCCESS == aiGetMaterialFloat(material, AI_MATKEY_SHININESS, &sf))
			matShininess = static_cast<float>(sf);
	}

	for (unsigned int i = 0; i < mesh->mNumVertices; i++)
	{
		Vertex vertex{};
		// position
		DirectX::XMVECTOR pos = DirectX::XMVectorSet(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z, 1.0f);
		pos = XMVector3TransformCoord(pos, xmTransform);
		XMStoreFloat3(&vertex.mPosition, pos);

		// normal (transform with inverse-transpose of 3x3 or use TransformNormal)
		if (mesh->mNormals)
		{
			DirectX::XMVECTOR n = DirectX::XMVectorSet(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z, 0.0f);
			n = XMVector3TransformNormal(n, xmTransform);
			n = DirectX::XMVector3Normalize(n);
			XMStoreFloat3(&vertex.mNormal, n);
		}
		else
		{
			vertex.mNormal = DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
		}

		// texcoords
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
	vector<vector<Texture>> loadedTextures =
	{
		loadMaterialTextures(material, aiTextureType_BASE_COLOR, TextureType::Albedo),
		loadMaterialTextures(material, aiTextureType_DIFFUSE, TextureType::Albedo), // fallback if BASE_COLOR not present
		loadMaterialTextures(material, aiTextureType_NORMALS, TextureType::Normal),
		loadMaterialTextures(material, aiTextureType_METALNESS, TextureType::Metalness),
		loadMaterialTextures(material, aiTextureType_DIFFUSE_ROUGHNESS, TextureType::Roughness)
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
		std::string texRef = str.C_Str();

		if (!texRef.empty() && texRef.rfind("./", 0) == 0)
			texRef = texRef.substr(2);

		if (!texRef.empty() && texRef[0] == '*')
		{
			std::cerr << "Embedded texture reference found (" << texRef << "). Embedded textures not handled here." << std::endl;
			continue;
		}

		std::string finalPath = texRef;
		std::filesystem::path pTex(texRef);
		bool found = false;

		if (pTex.is_absolute())
		{
			if (std::filesystem::exists(pTex))
			{
				finalPath = pTex.string();
				found = true;
			}
		}
		else
		{
			std::filesystem::path cand = std::filesystem::path(mDirectory) / pTex;
			if (std::filesystem::exists(cand))
			{
				finalPath = std::filesystem::relative(cand, std::filesystem::path(mDirectory)).string();
				found = true;
			}
			else
			{
				std::filesystem::path cand2 = std::filesystem::path(mDirectory) / "textures" / pTex;
				if (std::filesystem::exists(cand2))
				{
					finalPath = std::filesystem::relative(cand2, std::filesystem::path(mDirectory)).string();
					found = true;
				}
			}
		}

		if (!found)
		{
			finalPath = texRef;
			std::cerr << "Warning: texture file not found for reference '" << texRef << "'; using literal path." << std::endl;
		}

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
