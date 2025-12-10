#include "pch.h"
#include "CppUnitTest.h"
#include "RenderAPI/DataTypes.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace DirectX;

namespace RayTracingTests::DataTypes
{
	TEST_CLASS(LightDataTests)
	{
	public:
		
		TEST_METHOD(LightData_SizeIsMultipleOf16)
		{
			// GPU constant buffers require 16-byte alignment
			size_t size = sizeof(LightData);
			Assert::AreEqual(static_cast<size_t>(0), size % 16, L"LightData size must be multiple of 16 bytes for GPU");
		}

		TEST_METHOD(LightData_HasCorrectMembers)
		{
			LightData light;
			
			// Verify structure has expected members by setting values
			light.position = { 1.0f, 2.0f, 3.0f, 1.0f };
			light.dirType = { 0.0f, -1.0f, 0.0f, 0.0f };
			light.diffuseColor = { 1.0f, 1.0f, 1.0f, 1.0f };
			light.specularColor = { 1.0f, 1.0f, 1.0f, 1.0f };

			Assert::AreEqual(1.0f, light.position.x);
			Assert::AreEqual(2.0f, light.position.y);
			Assert::AreEqual(3.0f, light.position.z);
		}

		TEST_METHOD(LightData_PositionIsXMFLOAT4)
		{
			LightData light;
			light.position = { 5.0f, 10.0f, 15.0f, 1.0f };
			
			Assert::AreEqual(5.0f, light.position.x);
			Assert::AreEqual(10.0f, light.position.y);
			Assert::AreEqual(15.0f, light.position.z);
			Assert::AreEqual(1.0f, light.position.w);
		}

		TEST_METHOD(LightData_DirectionAndTypeStored)
		{
			LightData light;
			light.dirType = { 0.0f, -1.0f, 0.0f, 2.0f }; // direction + type in .w
			
			Assert::AreEqual(0.0f, light.dirType.x);
			Assert::AreEqual(-1.0f, light.dirType.y);
			Assert::AreEqual(0.0f, light.dirType.z);
			Assert::AreEqual(2.0f, light.dirType.w);
		}
	};

	TEST_CLASS(ConstantBufferDataTests)
	{
	public:
		
		TEST_METHOD(ConstantBufferData_HasReasonableSize)
		{
			// Constant buffer structure should exist and have calculable size
			size_t size = sizeof(ConstantBufferData);
			
			// Should be at least big enough for matrix + viewPos + lights array
			Assert::IsTrue(size >= 1600, L"ConstantBufferData should be at least 1600 bytes");
		}

		TEST_METHOD(ConstantBufferData_MaxLightsIs25)
		{
			ConstantBufferData cb;
			
			// Verify array size
			size_t lightArraySize = sizeof(cb.lights) / sizeof(LightData);
			Assert::AreEqual(static_cast<size_t>(cMaxLights), lightArraySize);
			Assert::AreEqual(25, cMaxLights);
		}

		TEST_METHOD(ConstantBufferData_NumLightsInitializes)
		{
			ConstantBufferData cb{};
			cb.numLights = 10;
			
			Assert::AreEqual(10, cb.numLights);
		}

		TEST_METHOD(ConstantBufferData_ViewPosIsXMFLOAT4)
		{
			ConstantBufferData cb;
			cb.viewPos = { 1.0f, 2.0f, 3.0f, 1.0f };
			
			Assert::AreEqual(1.0f, cb.viewPos.x);
			Assert::AreEqual(2.0f, cb.viewPos.y);
			Assert::AreEqual(3.0f, cb.viewPos.z);
		}

		TEST_METHOD(ConstantBufferData_LightsArrayAccessible)
		{
			ConstantBufferData cb;
			
			// Set first light
			cb.lights[0].position = { 10.0f, 20.0f, 30.0f, 1.0f };
			cb.lights[0].diffuseColor = { 1.0f, 0.0f, 0.0f, 1.0f };
			
			Assert::AreEqual(10.0f, cb.lights[0].position.x);
			Assert::AreEqual(1.0f, cb.lights[0].diffuseColor.x);
			Assert::AreEqual(0.0f, cb.lights[0].diffuseColor.y);
		}

		TEST_METHOD(ConstantBufferData_MatrixIsXMMATRIX)
		{
			ConstantBufferData cb;
			cb.vpMatrix = XMMatrixIdentity();
			
			// Check identity matrix
			Assert::AreEqual(1.0f, XMVectorGetX(cb.vpMatrix.r[0]));
			Assert::AreEqual(1.0f, XMVectorGetY(cb.vpMatrix.r[1]));
		}
	};

	TEST_CLASS(MeshMaterialDataTests)
	{
	public:
		
		TEST_METHOD(MeshMaterialData_DefaultBaseColorIsWhite)
		{
			MeshMaterialData material;
			
			Assert::AreEqual(1.0f, material.baseColorFactor.x);
			Assert::AreEqual(1.0f, material.baseColorFactor.y);
			Assert::AreEqual(1.0f, material.baseColorFactor.z);
			Assert::AreEqual(1.0f, material.baseColorFactor.w);
		}

		TEST_METHOD(MeshMaterialData_DefaultMetalnessIsOne)
		{
			MeshMaterialData material;
			Assert::AreEqual(1.0f, material.metalnessFactor);
		}

		TEST_METHOD(MeshMaterialData_DefaultRoughnessIsOne)
		{
			MeshMaterialData material;
			Assert::AreEqual(1.0f, material.roughnessFactor);
		}

		TEST_METHOD(MeshMaterialData_DefaultAlphaCutoffIsHalf)
		{
			MeshMaterialData material;
			Assert::AreEqual(0.5f, material.alphaCutoff);
		}

		TEST_METHOD(MeshMaterialData_DefaultEmissiveIsBlack)
		{
			MeshMaterialData material;
			
			Assert::AreEqual(0.0f, material.emissiveFactor.x);
			Assert::AreEqual(0.0f, material.emissiveFactor.y);
			Assert::AreEqual(0.0f, material.emissiveFactor.z);
		}

		TEST_METHOD(MeshMaterialData_SizeIsMultipleOf16)
		{
			// GPU constant buffers require 16-byte alignment
			size_t size = sizeof(MeshMaterialData);
			Assert::AreEqual(static_cast<size_t>(0), size % 16, L"MeshMaterialData must be 16-byte aligned");
		}

		TEST_METHOD(MeshMaterialData_CanSetCustomValues)
		{
			MeshMaterialData material;
			
			material.baseColorFactor = { 0.5f, 0.5f, 0.5f, 1.0f };
			material.metalnessFactor = 0.8f;
			material.roughnessFactor = 0.3f;
			material.alphaCutoff = 0.1f;
			material.emissiveFactor = { 1.0f, 0.5f, 0.0f, 1.0f };

			Assert::AreEqual(0.5f, material.baseColorFactor.x);
			Assert::AreEqual(0.8f, material.metalnessFactor);
			Assert::AreEqual(0.3f, material.roughnessFactor);
			Assert::AreEqual(0.1f, material.alphaCutoff);
			Assert::AreEqual(1.0f, material.emissiveFactor.x);
		}

		TEST_METHOD(MeshMaterialData_MetalnessRangeValid)
		{
			MeshMaterialData material;
			
			// Metalness should be 0.0-1.0
			material.metalnessFactor = 0.0f;
			Assert::IsTrue(material.metalnessFactor >= 0.0f && material.metalnessFactor <= 1.0f);
			
			material.metalnessFactor = 1.0f;
			Assert::IsTrue(material.metalnessFactor >= 0.0f && material.metalnessFactor <= 1.0f);
		}
	};
}
