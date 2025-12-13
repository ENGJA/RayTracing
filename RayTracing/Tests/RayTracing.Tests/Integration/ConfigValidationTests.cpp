#include "pch.h"
#include "CppUnitTest.h"
#include "config.h"
#include "RenderAPI/DataTypes.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace DirectX;

namespace RayTracingTests::Integration
{
	TEST_CLASS(ConfigValidationTests)
	{
	public:
		
		TEST_METHOD(Config_BufferCountMatchesFrameCount)
		{
			Assert::AreEqual(Config::cBufferCount, Config::cFrameCount,
			                 L"Buffer count should match frame count for proper synchronization");
		}

		TEST_METHOD(Config_TextureSlotsReasonable)
		{
			Assert::IsTrue(Config::cNumberOfTextureSlots >= 5, 
			               L"Should support at least 5 texture slots (base, normal, metallic, roughness, emissive)");
		}

		TEST_METHOD(Config_SrvDescriptorsEnoughForTextures)
		{
			// Each material can have up to cNumberOfTextureSlots textures
			// We should have enough SRV descriptors
			UINT minDescriptorsNeeded = Config::cNumberOfTextureSlots * 10; // At least 10 materials
			Assert::IsTrue(Config::cNumberOfSrvDescriptors >= minDescriptorsNeeded,
			               L"SRV descriptors should be enough for multiple materials");
		}

		TEST_METHOD(DataTypes_LightArraySizeMatchesConstant)
		{
			ConstantBufferData cb;
			size_t arraySize = sizeof(cb.lights) / sizeof(LightData);
			Assert::AreEqual(static_cast<size_t>(cMaxLights), arraySize,
			                 L"Light array size should match cMaxLights constant");
		}

		TEST_METHOD(DataTypes_ConstantBufferAlignment)
		{
			// While CB doesn't need to be 256-aligned in size, elements should be 16-byte aligned
			Assert::AreEqual(static_cast<size_t>(0), sizeof(LightData) % 16,
			                 L"LightData should be 16-byte aligned");
			Assert::AreEqual(static_cast<size_t>(0), sizeof(MeshMaterialData) % 16,
			                 L"MeshMaterialData should be 16-byte aligned");
		}

		TEST_METHOD(Integration_ConfigAndDataTypesConsistency)
		{
			// Verify that configuration makes sense with data types
			size_t cbSize = sizeof(ConstantBufferData);
			
			// CB size should be reasonable (not too small, not gigantic)
			Assert::IsTrue(cbSize > 1000, L"ConstantBuffer should be at least 1KB");
			Assert::IsTrue(cbSize < 100000, L"ConstantBuffer shouldn't be > 100KB");
			
			// Buffer count should be at least 2 for double buffering
			Assert::IsTrue(Config::cBufferCount >= 2, 
			               L"Should have at least 2 buffers for double buffering");
		}
	};
}
