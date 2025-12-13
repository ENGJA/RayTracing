#include "pch.h"
#include "CppUnitTest.h"
#include "ResourceLoading/ImageDecoder.h"
#include <fstream>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace RayTracingTests::Integration
{
	TEST_CLASS(ImageDecoderTests)
	{
	public:
		
		TEST_METHOD(ImageDecoder_HasAlphaChannel_JPEGReturnsFalse)
		{
			// JPEG files don't support alpha channel
			bool hasAlpha = ImageDecoder::HasAlphaChannel(L"test.jpg");
			Assert::IsFalse(hasAlpha);
		}

		TEST_METHOD(ImageDecoder_HasAlphaChannel_BMPReturnsFalse)
		{
			// BMP files don't support alpha channel (in standard format)
			bool hasAlpha = ImageDecoder::HasAlphaChannel(L"test.bmp");
			Assert::IsFalse(hasAlpha);
		}

		TEST_METHOD(ImageDecoder_HasAlphaChannel_HandlesExtensionCheck)
		{
			// Test various extensions
			Assert::IsFalse(ImageDecoder::HasAlphaChannel(L"image.jpeg"));
			Assert::IsFalse(ImageDecoder::HasAlphaChannel(L"image.JPEG")); // uppercase
			Assert::IsFalse(ImageDecoder::HasAlphaChannel(L"image.JPG"));
		}
	};
}
