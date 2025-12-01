#include "pch.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/GltfMaterial.h>

#include "Model.h"
#include <RenderAPI/DataTypes.h>
#include "ResourceLoading/ImageDecoder.h"
using std::string, std::vector, std::cerr, std::endl;



AlphaProperties Model::GetAlphaProperties(const aiMaterial* material)
{
	AlphaProperties props;
	aiString alphaModeStr;
	if (material->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaModeStr) == AI_SUCCESS)
	{
		std::string modeStr(alphaModeStr.C_Str());
		if (modeStr == "MASK")
		{
			props.mRenderLayer = RenderLayer::Masked;
			float cutoff = 0.5f;
			if (material->Get(AI_MATKEY_GLTF_ALPHACUTOFF, cutoff) == AI_SUCCESS)
				props.alphaCutoff = cutoff;
		}
		else if (modeStr == "BLEND")
		{
			props.mRenderLayer = RenderLayer::Blend;
		}
		return props;
	}

	RenderLayer layer = RenderLayer::Opaque;
	float opacity = 1.0f;
	aiString texPath;
	aiGetMaterialFloat(material, AI_MATKEY_OPACITY, &opacity);
	if (opacity < 1.0f)
	{
		layer = RenderLayer::Blend;
	}
	else if (AI_SUCCESS == material->GetTexture(aiTextureType_BASE_COLOR, 0, &texPath) ||
		AI_SUCCESS == material->GetTexture(aiTextureType_DIFFUSE, 0, &texPath))
	{
		std::string finalPath = ResolveTexturePath(texPath.C_Str());
		std::wstring wPath(finalPath.begin(), finalPath.end());

		if (ImageDecoder::HasAlphaChannel(wPath))
			layer = RenderLayer::Masked;
	}

	props.mRenderLayer = layer;
	return props;
}


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
		aiProcess_PreTransformVertices |
		aiProcess_MakeLeftHanded |
		aiProcess_FlipWindingOrder;
	const aiScene* scene = importer.ReadFile(path, flags);

	int tangentSpaceHandednessMultiplier  = 1;
	string ext = std::filesystem::path(path).extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
	if (ext == ".gltf" || ext == ".glb" || ext == ".blend")
		tangentSpaceHandednessMultiplier  = -1;

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

	mLights.clear();
	if (scene->mNumLights > 0)
	{
		for (unsigned int li = 0; li < scene->mNumLights && mLights.size() < cMaxLights; ++li)
		{
			aiLight* aLight = scene->mLights[li];
			LightData ld{};
			ld.color = DirectX::XMFLOAT4(
				1.0f,
				0.8f,
				0.3f,
				0.5f 
			);

			if (aLight->mType == aiLightSource_DIRECTIONAL)
			{
				ld.dirType = DirectX::XMFLOAT4(
					aLight->mDirection.x,
					aLight->mDirection.y,
					aLight->mDirection.z,
					1.0f 
				);
				ld.position = DirectX::XMFLOAT4(0, 0, 0, 0);
			}
			else 
			{
				ld.position = DirectX::XMFLOAT4(
					aLight->mPosition.x,
					aLight->mPosition.y,
					aLight->mPosition.z,
					1.0f 
				);
				ld.dirType = DirectX::XMFLOAT4(
					aLight->mDirection.x,
					aLight->mDirection.y,
					aLight->mDirection.z,
					0.0f 
				);
			}

			mLights.push_back(ld);
		}
	}

	processNode(scene->mRootNode, scene, identity, tangentSpaceHandednessMultiplier );
}

void Model::processNode(aiNode* node, const aiScene* scene, const aiMatrix4x4& parentTransform, int tangentSpaceHandednessMultiplier)
{
	aiMatrix4x4 currentTransform = parentTransform * node->mTransformation;

	for (unsigned int i = 0; i < node->mNumMeshes; i++)
	{
		aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];

		aiMaterial* mat = scene->mMaterials[mesh->mMaterialIndex];
		const float alphaThreshold = 0.999f; 

		//aiColor4D diffuseColor;
		//if (AI_SUCCESS == aiGetMaterialColor(mat, AI_MATKEY_COLOR_DIFFUSE, &diffuseColor))
		//{
		//	if (diffuseColor.a < alphaThreshold)
		//		continue;
		//}

		mMeshes.push_back(processMesh(mesh, scene, currentTransform, tangentSpaceHandednessMultiplier));
	}
	for (unsigned int i = 0; i < node->mNumChildren; i++)
		processNode(node->mChildren[i], scene, currentTransform, tangentSpaceHandednessMultiplier);
}

Mesh Model::processMesh(aiMesh* mesh, const aiScene* scene, const aiMatrix4x4& transform, int tangentSpaceHandednessMultiplier)
{
	vector<Vertex> vertices;
	vector<unsigned int> indices;
	vector<Texture> textures;

	DirectX::XMMATRIX xmTransform = AiToXMMatrix(transform);
	DirectX::XMMATRIX normalMatrix = DirectX::XMMatrixTranspose(DirectX::XMMatrixInverse(nullptr, xmTransform));

	DirectX::XMVECTOR minV = DirectX::XMVectorSet(FLT_MAX, FLT_MAX, FLT_MAX, 0);
	DirectX::XMVECTOR maxV = DirectX::XMVectorSet(-FLT_MAX, -FLT_MAX, -FLT_MAX, 0);

	for (unsigned int i = 0; i < mesh->mNumVertices; i++)
	{
		Vertex vertex{};
		// position
		{
			DirectX::XMVECTOR pos = DirectX::XMVectorSet(mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z, 1.0f);
			pos = XMVector3TransformCoord(pos, xmTransform);
			XMStoreFloat3(&vertex.mPosition, pos);
		}

		// normal (transform with inverse-transpose of 3x3)
		if (mesh->mNormals)
		{
			DirectX::XMVECTOR n = DirectX::XMVectorSet(mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z, 0.0f);

			if (mesh->mTangents)
			{
				DirectX::XMVECTOR t = DirectX::XMVectorSet(mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z, 0.0f);
				DirectX::XMVECTOR b = DirectX::XMVectorSet(mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z, 0.0f);

				DirectX::XMVECTOR crossNT = DirectX::XMVector3Cross(n, t);
				float dotValue = DirectX::XMVectorGetX(DirectX::XMVector3Dot(crossNT, b));
				float handedness = tangentSpaceHandednessMultiplier  * ((dotValue < 0.0f) ? -1.0f : 1.0f);

				t = XMVector3TransformNormal(t, normalMatrix);
				t = DirectX::XMVector3Normalize(t);

				DirectX::XMFLOAT3 tFloat3{};
				XMStoreFloat3(&tFloat3, t);
				vertex.mTangent = DirectX::XMFLOAT4(tFloat3.x, tFloat3.y, tFloat3.z, handedness);
			}
			else 
			{
				vertex.mTangent = DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 1.0f };
			}

			n = XMVector3TransformNormal(n, normalMatrix);
			n = DirectX::XMVector3Normalize(n);
			XMStoreFloat3(&vertex.mNormal, n);
		}
		else
		{
			vertex.mNormal = DirectX::XMFLOAT3{ 0.0f, 0.0f, 0.0f };
			vertex.mTangent = DirectX::XMFLOAT4{ 0.0f, 0.0f, 0.0f, 1.0f };
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

		{
			DirectX::XMVECTOR pos = DirectX::XMLoadFloat3(&vertex.mPosition);
			minV = DirectX::XMVectorMin(minV, pos);
			maxV = DirectX::XMVectorMax(maxV, pos);
		}

		vertices.push_back(vertex);
	}

	// Calculate Center for sorting
	DirectX::XMVECTOR centerV = DirectX::XMVectorAdd(minV, maxV);
	centerV = DirectX::XMVectorScale(centerV, 0.5f);
	DirectX::XMFLOAT3 center;
	XMStoreFloat3(&center, centerV);

	
	for (unsigned int i = 0; i < mesh->mNumFaces; i++)
	{
		aiFace face = mesh->mFaces[i];
		for (unsigned int j = 0; j < face.mNumIndices; j++)
			indices.push_back(face.mIndices[j]);
	}

	// get material properties
	MeshMaterialData matData;
	AlphaProperties alphaProps;
	aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];
	if (material)
	{
		alphaProps = GetAlphaProperties(material);
		aiColor4D color;
		if (AI_SUCCESS == aiGetMaterialColor(material, AI_MATKEY_BASE_COLOR, &color)
			|| AI_SUCCESS == aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &color))
		{
			matData.baseColorFactor = { color.r, color.g, color.b, color.a };
		}

		if (AI_SUCCESS == aiGetMaterialColor(material, AI_MATKEY_COLOR_EMISSIVE, &color))
			matData.emissiveFactor = { color.r, color.g, color.b, 1.0f };

		ai_real f;
		if (AI_SUCCESS == aiGetMaterialFloat(material, AI_MATKEY_METALLIC_FACTOR, &f))
			matData.metalnessFactor = static_cast<float>(f);
		if (AI_SUCCESS == aiGetMaterialFloat(material, AI_MATKEY_ROUGHNESS_FACTOR, &f))
			matData.roughnessFactor = static_cast<float>(f);
		if (AI_SUCCESS == aiGetMaterialFloat(material, AI_MATKEY_EMISSIVE_INTENSITY, &f))
		{
			DirectX::XMVECTOR v = DirectX::XMLoadFloat4(&matData.emissiveFactor);
			v = DirectX::XMVectorScale(v, static_cast<float>(f));
			DirectX::XMStoreFloat4(&matData.emissiveFactor, v);
		}
	}

	matData.alphaCutoff = alphaProps.alphaCutoff;

	// get PBR material textures
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

	return Mesh(vertices, indices, textures, center, matData, alphaProps.mRenderLayer);
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

		std::string finalPath = ResolveTexturePath(texRef);

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

std::string Model::ResolveTexturePath(const std::string& rawPath)
{
	std::string texRef = rawPath;
	if (!texRef.empty() && texRef.rfind("./", 0) == 0) texRef = texRef.substr(2);

	// 1. Check absolute
	std::filesystem::path pTex(texRef);
	if (pTex.is_absolute() && std::filesystem::exists(pTex)) return pTex.string();

	// 2. Check relative to model
	std::filesystem::path cand = std::filesystem::path(mDirectory) / pTex;
	if (std::filesystem::exists(cand)) return cand.string();

	// 3. Check 'textures' folder
	std::filesystem::path cand2 = std::filesystem::path(mDirectory) / "textures" / pTex;
	if (std::filesystem::exists(cand2)) return cand2.string();

	std::cerr << "Warning: texture file not found for reference '" << rawPath << "'; using literal path." << std::endl;
	return rawPath;
}
