#include "pch.h"
#include "ImageDecoder.h"
#include "helpers.h"


using Microsoft::WRL::ComPtr;

DecodedImage ImageDecoder::DecodeImageRGBA8_ThreadSafe(const std::wstring& path)
{
	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	ASSERT_HR(hr, L"Failed to initialize COM for image decoding.");

	ComPtr<IWICImagingFactory> wicFactory;
	hr = CoCreateInstance(
		CLSID_WICImagingFactory,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(wicFactory.ReleaseAndGetAddressOf()));
	ASSERT_HR(hr, L"Failed to create WIC factory");

	ComPtr<IWICBitmapDecoder> decoder;
	hr = wicFactory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.ReleaseAndGetAddressOf());
	ASSERT_HR(hr, L"Failed to open image file");

	ComPtr<IWICBitmapFrameDecode> frame;
	hr = decoder->GetFrame(0, frame.ReleaseAndGetAddressOf());
	ASSERT_HR(hr, L"Failed to decode image frame");

	UINT width = 0, height = 0;
	hr = frame->GetSize(&width, &height);
	ASSERT_HR(hr, L"Failed to get image size");

	WICPixelFormatGUID srcFormat{};
	hr = frame->GetPixelFormat(&srcFormat);
	ASSERT_HR(hr, L"Failed to get pixel format");

	static WICPixelFormatGUID target = GUID_WICPixelFormat32bppRGBA;
	bool needsConvert = (srcFormat != target);

	ComPtr<IWICFormatConverter> converter;
	if (needsConvert)
	{
		hr = wicFactory->CreateFormatConverter(converter.ReleaseAndGetAddressOf());
		ASSERT_HR(hr, L"Failed to create format converter");
		hr = converter->Initialize(frame.Get(), target, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom);
		ASSERT_HR(hr, L"Failed to init format converter");
	}

	const UINT bpp = 4; // RGBA8
	const UINT rowPitch = width * bpp;
	std::vector<BYTE> pixels;
	pixels.resize(static_cast<size_t>(rowPitch) * height);

	WICRect rect{ 0, 0, static_cast<INT>(width), static_cast<INT>(height) };
	if (needsConvert)
		hr = converter->CopyPixels(&rect, rowPitch, static_cast<UINT>(pixels.size()), pixels.data());
	else
		hr = frame->CopyPixels(&rect, rowPitch, static_cast<UINT>(pixels.size()), pixels.data());
	ASSERT_HR(hr, L"Failed to copy pixels");


	CoUninitialize();

	return
	{
	.width = width,
	.height = height,
	.pixels = std::move(pixels)
	};
}