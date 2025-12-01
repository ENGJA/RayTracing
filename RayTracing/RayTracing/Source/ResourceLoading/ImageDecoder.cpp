#include "pch.h"
#include "ImageDecoder.h"
#include "helpers.h"


using Microsoft::WRL::ComPtr;

struct COMInitializer
{
	bool needsUninit;
	COMInitializer(HRESULT hr) : needsUninit(hr == S_OK) {}
	~COMInitializer() { if (needsUninit) CoUninitialize(); }
};

DecodedImage ImageDecoder::DecodeImageRGBA8_ThreadSafe(const std::wstring& path)
{
	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
		ASSERT_HR(hr, L"Failed to initialize COM for image decoding.");
	
	COMInitializer comInitialized(hr);

	ComPtr<IWICImagingFactory> wicFactory;
	hr = CoCreateInstance(
		CLSID_WICImagingFactory,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(wicFactory.GetAddressOf()));
	ASSERT_HR(hr, L"Failed to create WIC factory");

	ComPtr<IWICBitmapDecoder> decoder;
	hr = wicFactory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.GetAddressOf());
	ASSERT_HR(hr, L"Failed to open image file");

	ComPtr<IWICBitmapFrameDecode> frame;
	hr = decoder->GetFrame(0, frame.GetAddressOf());
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
		hr = wicFactory->CreateFormatConverter(converter.GetAddressOf());
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

	return
	{
	.width = width,
	.height = height,
	.pixels = std::move(pixels)
	};
}

bool ImageDecoder::HasAlphaChannel(const std::wstring& path)
{
	std::filesystem::path p(path);
	std::string ext = p.extension().string();
	std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
	if (ext == ".jpg" || ext == ".jpeg" || ext == ".bmp")
	{
		return false; // These formats do not support alpha channel
	}

	HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(hr) && hr != RPC_E_CHANGED_MODE)
		ASSERT_HR(hr, L"Failed to initialize COM for image decoding.");

	COMInitializer comInitialized(hr);
	ComPtr<IWICImagingFactory> wicFactory;
	hr = CoCreateInstance(
		CLSID_WICImagingFactory,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(wicFactory.GetAddressOf()));

	ASSERT_HR(hr, L"Failed to create WIC factory");

	ComPtr<IWICBitmapDecoder> decoder;
	hr = wicFactory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.ReleaseAndGetAddressOf());
	ASSERT_HR(hr, L"Failed to open image file");

	ComPtr<IWICBitmapFrameDecode> frame;
	hr = decoder->GetFrame(0, frame.ReleaseAndGetAddressOf());
	ASSERT_HR(hr, L"Failed to decode image frame");

	WICPixelFormatGUID srcFormat{};
	hr = frame->GetPixelFormat(&srcFormat);
	ASSERT_HR(hr, L"Failed to get pixel format");

	// Check if the pixel format implies an alpha channel
	if (srcFormat == GUID_WICPixelFormat32bppRGBA ||
		srcFormat == GUID_WICPixelFormat32bppPRGBA ||
		srcFormat == GUID_WICPixelFormat32bppBGRA ||
		srcFormat == GUID_WICPixelFormat32bppPBGRA ||
		srcFormat == GUID_WICPixelFormat64bppRGBA ||
		srcFormat == GUID_WICPixelFormat64bppPRGBA ||
		srcFormat == GUID_WICPixelFormat64bppBGRA ||
		srcFormat == GUID_WICPixelFormat64bppPBGRA ||
		srcFormat == GUID_WICPixelFormat128bppRGBAFloat ||
		srcFormat == GUID_WICPixelFormat128bppPRGBAFloat ||
		srcFormat == GUID_WICPixelFormat128bppRGBFloat || // Sometimes used for HDR
		srcFormat == GUID_WICPixelFormat40bppCMYKAlpha ||
		srcFormat == GUID_WICPixelFormat80bppCMYKAlpha)
	{
		return true;
	}
	if (srcFormat == GUID_WICPixelFormat1bppIndexed ||
		srcFormat == GUID_WICPixelFormat2bppIndexed ||
		srcFormat == GUID_WICPixelFormat4bppIndexed ||
		srcFormat == GUID_WICPixelFormat8bppIndexed)
	{
		ComPtr<IWICPalette> palette;
		hr = wicFactory->CreatePalette(palette.GetAddressOf());
		if (SUCCEEDED(hr))
		{
			hr = frame->CopyPalette(palette.Get());
			if (SUCCEEDED(hr))
			{
				BOOL hasAlpha = FALSE;
				palette->HasAlpha(&hasAlpha);
				return hasAlpha == TRUE;
			}
		}
	}

	return false;
}
