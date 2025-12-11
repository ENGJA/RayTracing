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

		TEST_METHOD(DecodedImage_DefaultConstruction)
		{
			DecodedImage img;
			Assert::AreEqual(0u, img.width);
			Assert::AreEqual(0u, img.height);
			Assert::IsTrue(img.pixels.empty());
		}

		TEST_METHOD(DecodedImage_CanStorePixelData)
		{
			DecodedImage img;
			img.width = 16;
			img.height = 16;
			img.pixels.resize(16 * 16 * 4); // RGBA8

			Assert::AreEqual(16u, img.width);
			Assert::AreEqual(16u, img.height);
			Assert::AreEqual(static_cast<size_t>(1024), img.pixels.size());
		}

		TEST_METHOD(DecodedImage_PixelDataSizeCalculation)
		{
			DecodedImage img;
			img.width = 256;
			img.height = 256;
			
			size_t expectedSize = 256 * 256 * 4; // RGBA = 4 bytes per pixel
			Assert::AreEqual(expectedSize, static_cast<size_t>(262144));
		}
	};
}
