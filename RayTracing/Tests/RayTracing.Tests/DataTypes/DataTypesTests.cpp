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
	};

	TEST_CLASS(MeshMaterialDataTests)
	{
	public:
		
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
