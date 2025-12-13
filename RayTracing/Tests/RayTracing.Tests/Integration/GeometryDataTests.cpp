#include "pch.h"
#include "CppUnitTest.h"
#include "ResourceLoading/Vertex.h"
#include "RenderAPI/DataTypes.h"
#include <vector>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace DirectX;

namespace RayTracingTests::Integration
{
	TEST_CLASS(GeometryDataTests)
	{
	public:
		
		TEST_METHOD(Geometry_CalculateNormal_Triangle)
		{
			// Create a triangle in XY plane
			XMFLOAT3 p1 = { 0.0f, 0.0f, 0.0f };
			XMFLOAT3 p2 = { 1.0f, 0.0f, 0.0f };
			XMFLOAT3 p3 = { 0.0f, 1.0f, 0.0f };
			
			// Calculate normal: (p2-p1) x (p3-p1)
			XMVECTOR v1 = XMLoadFloat3(&p1);
			XMVECTOR v2 = XMLoadFloat3(&p2);
			XMVECTOR v3 = XMLoadFloat3(&p3);
			
			XMVECTOR edge1 = XMVectorSubtract(v2, v1);
			XMVECTOR edge2 = XMVectorSubtract(v3, v1);
			XMVECTOR normal = XMVector3Normalize(XMVector3Cross(edge1, edge2));
			
			XMFLOAT3 result;
			XMStoreFloat3(&result, normal);
			
			// Normal should point in +Z direction
			Assert::AreEqual(0.0f, result.x, 0.001f);
			Assert::AreEqual(0.0f, result.y, 0.001f);
			Assert::AreEqual(1.0f, result.z, 0.001f);
		}

		TEST_METHOD(Geometry_VertexSizeConsistency)
		{
			size_t vertexSize = sizeof(Vertex);
			
			// Vertex should have predictable size
			// Position (12) + Normal (12) + TexCoords (8) + Tangent (16) + MaterialProps (8) = 56 bytes minimum
			Assert::IsTrue(vertexSize >= 56, L"Vertex should be at least 56 bytes");
			Assert::IsTrue(vertexSize <= 128, L"Vertex shouldn't be > 128 bytes");
		}

		TEST_METHOD(Geometry_CalculateTangent_Simple)
		{
			// Simple tangent calculation for a quad
			XMFLOAT3 pos1 = { 0.0f, 0.0f, 0.0f };
			XMFLOAT3 pos2 = { 1.0f, 0.0f, 0.0f };
			XMFLOAT3 pos3 = { 0.0f, 1.0f, 0.0f };
			
			XMFLOAT2 uv1 = { 0.0f, 0.0f };
			XMFLOAT2 uv2 = { 1.0f, 0.0f };
			XMFLOAT2 uv3 = { 0.0f, 1.0f };
			
			// Delta pos
			XMVECTOR dp1 = XMVectorSubtract(XMLoadFloat3(&pos2), XMLoadFloat3(&pos1));
			XMVECTOR dp2 = XMVectorSubtract(XMLoadFloat3(&pos3), XMLoadFloat3(&pos1));
			
			// Delta UV
			float duv1x = uv2.x - uv1.x; // 1.0
			float duv1y = uv2.y - uv1.y; // 0.0
			float duv2x = uv3.x - uv1.x; // 0.0
			float duv2y = uv3.y - uv1.y; // 1.0
			
			float det = duv1x * duv2y - duv1y * duv2x; // 1.0
			
			if (std::abs(det) > 0.0001f)
			{
				float invDet = 1.0f / det;
				XMVECTOR tangent = XMVectorScale(XMVectorSubtract(
					XMVectorScale(dp1, duv2y),
					XMVectorScale(dp2, duv1y)
				), invDet);
				
				tangent = XMVector3Normalize(tangent);
				
				XMFLOAT3 t;
				XMStoreFloat3(&t, tangent);
				
				// Tangent should point in +X direction
				Assert::AreEqual(1.0f, t.x, 0.01f);
				Assert::AreEqual(0.0f, t.y, 0.01f);
			}
		}

		TEST_METHOD(Geometry_BoundingBoxCalculation)
		{
			std::vector<Vertex> vertices;
			
			Vertex v1, v2, v3;
			v1.mPosition = { -1.0f, -1.0f, -1.0f };
			v2.mPosition = { 1.0f, 1.0f, 1.0f };
			v3.mPosition = { 0.0f, 0.0f, 0.0f };
			
			vertices.push_back(v1);
			vertices.push_back(v2);
			vertices.push_back(v3);
			
			// Calculate bounding box
			XMFLOAT3 minBounds = { FLT_MAX, FLT_MAX, FLT_MAX };
			XMFLOAT3 maxBounds = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
			
			for (const auto& vertex : vertices)
			{
				minBounds.x = (std::min)(minBounds.x, vertex.mPosition.x);
				minBounds.y = (std::min)(minBounds.y, vertex.mPosition.y);
				minBounds.z = (std::min)(minBounds.z, vertex.mPosition.z);
				
				maxBounds.x = (std::max)(maxBounds.x, vertex.mPosition.x);
				maxBounds.y = (std::max)(maxBounds.y, vertex.mPosition.y);
				maxBounds.z = (std::max)(maxBounds.z, vertex.mPosition.z);
			}
			
			Assert::AreEqual(-1.0f, minBounds.x);
			Assert::AreEqual(-1.0f, minBounds.y);
			Assert::AreEqual(-1.0f, minBounds.z);
			Assert::AreEqual(1.0f, maxBounds.x);
			Assert::AreEqual(1.0f, maxBounds.y);
			Assert::AreEqual(1.0f, maxBounds.z);
		}
	};
}
