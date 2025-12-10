#include "pch.h"
#include "CppUnitTest.h"
#include "config.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace RayTracingTests::Configuration
{
	TEST_CLASS(ConfigTests)
	{
	public:
		
		TEST_METHOD(Config_BufferCountIsTwo)
		{
			Assert::AreEqual(2u, Config::cBufferCount);
		}

		TEST_METHOD(Config_FrameCountEqualsBufferCount)
		{
			Assert::AreEqual(Config::cBufferCount, Config::cFrameCount);
		}

		TEST_METHOD(Config_BackBufferFormatIsRGBA8)
		{
			Assert::AreEqual(static_cast<int>(DXGI_FORMAT_R8G8B8A8_UNORM), 
			                 static_cast<int>(Config::cBackBufferFormat));
		}

		TEST_METHOD(Config_DepthBufferFormatIsD32Float)
		{
			Assert::AreEqual(static_cast<int>(DXGI_FORMAT_D32_FLOAT), 
			                 static_cast<int>(Config::cDepthBufferFormat));
		}

		TEST_METHOD(Config_NumberOfTextureSlotsIsFive)
		{
			Assert::AreEqual(5u, Config::cNumberOfTextureSlots);
		}

		TEST_METHOD(Config_NumberOfSrvDescriptorsIs4096)
		{
			Assert::AreEqual(4096u, Config::cNumberOfSrvDescriptors);
		}

		TEST_METHOD(Config_AllValuesAreConstexpr)
		{
			// If this compiles, constexpr works correctly
			constexpr UINT bufferCount = Config::cBufferCount;
			constexpr UINT frameCount = Config::cFrameCount;
			constexpr DXGI_FORMAT backBufferFormat = Config::cBackBufferFormat;
			constexpr DXGI_FORMAT depthBufferFormat = Config::cDepthBufferFormat;
			constexpr UINT textureSlots = Config::cNumberOfTextureSlots;
			constexpr UINT srvDescriptors = Config::cNumberOfSrvDescriptors;

			Assert::IsTrue(bufferCount == 2);
			Assert::IsTrue(frameCount == 2);
			Assert::IsTrue(textureSlots == 5);
			Assert::IsTrue(srvDescriptors == 4096);
		}

		TEST_METHOD(Config_BufferCountIsPositive)
		{
			Assert::IsTrue(Config::cBufferCount > 0, L"Buffer count must be positive");
		}

		TEST_METHOD(Config_TextureSlotsIsPositive)
		{
			Assert::IsTrue(Config::cNumberOfTextureSlots > 0, L"Texture slots must be positive");
		}

		TEST_METHOD(Config_SrvDescriptorsIsReasonable)
		{
			Assert::IsTrue(Config::cNumberOfSrvDescriptors >= 256, L"Should have at least 256 SRV descriptors");
			Assert::IsTrue(Config::cNumberOfSrvDescriptors <= 1000000, L"Should have reasonable upper limit");
		}
	};
}
