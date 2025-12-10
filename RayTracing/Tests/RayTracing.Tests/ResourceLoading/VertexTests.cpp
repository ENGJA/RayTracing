#include "pch.h"
#include "CppUnitTest.h"
#include "ResourceLoading/Vertex.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace DirectX;

namespace RayTracingTests::ResourceLoading
{
	TEST_CLASS(VertexTests)
	{
	public:
		
		TEST_METHOD(Vertex_DefaultConstructor)
		{
			Vertex v{};
			Assert::AreEqual(0.0f, v.mPosition.x);
			Assert::AreEqual(0.0f, v.mPosition.y);
			Assert::AreEqual(0.0f, v.mPosition.z);
		}

		TEST_METHOD(Vertex_InitializesCorrectly)
		{
			Vertex v;
			v.mPosition = { 1.0f, 2.0f, 3.0f };
			v.mNormal = { 0.0f, 1.0f, 0.0f };
			v.mTexCoords = { 0.5f, 0.5f };
			v.mTangent = { 1.0f, 0.0f, 0.0f, 1.0f };

			Assert::AreEqual(1.0f, v.mPosition.x);
			Assert::AreEqual(2.0f, v.mPosition.y);
			Assert::AreEqual(3.0f, v.mPosition.z);
			Assert::AreEqual(0.5f, v.mTexCoords.x);
		}

		TEST_METHOD(Vertex_MaterialPropsDefaultValues)
		{
			Vertex v;
			Assert::AreEqual(0.0f, v.mMaterialProps.x);   // metalness
			Assert::AreEqual(32.0f, v.mMaterialProps.y);  // shininess
		}

		TEST_METHOD(Vertex_NormalCanBeSet)
		{
			Vertex v;
			v.mNormal = { 0.0f, 1.0f, 0.0f };

			Assert::AreEqual(0.0f, v.mNormal.x);
			Assert::AreEqual(1.0f, v.mNormal.y);
			Assert::AreEqual(0.0f, v.mNormal.z);
		}

		TEST_METHOD(Vertex_TangentHasFourComponents)
		{
			Vertex v;
			v.mTangent = { 1.0f, 0.0f, 0.0f, 1.0f };

			Assert::AreEqual(1.0f, v.mTangent.x);
			Assert::AreEqual(0.0f, v.mTangent.y);
			Assert::AreEqual(0.0f, v.mTangent.z);
			Assert::AreEqual(1.0f, v.mTangent.w);  // handedness
		}

		TEST_METHOD(Vertex_TexCoordsCanBeSet)
		{
			Vertex v;
			v.mTexCoords = { 0.25f, 0.75f };

			Assert::AreEqual(0.25f, v.mTexCoords.x);
			Assert::AreEqual(0.75f, v.mTexCoords.y);
		}

		TEST_METHOD(NormalizeVector_WorksCorrectly)
		{
			XMFLOAT3 vec{ 3.0f, 4.0f, 0.0f };
			XMVECTOR v = XMLoadFloat3(&vec);
			v = XMVector3Normalize(v);
			
			XMFLOAT3 result;
			XMStoreFloat3(&result, v);

			float length = std::sqrt(result.x * result.x + result.y * result.y + result.z * result.z);
			Assert::AreEqual(1.0f, length, 0.001f, L"Normalized vector should have length 1");
		}
	};
}
