#pragma once

struct DecodedImage
{
	UINT width{};
	UINT height{};
	std::vector<BYTE> pixels; // RGBA8
};
class ImageDecoder
{
public:
	/**
	 * @brief Decodes an image file into RGBA8 pixel data in a thread-safe manner.
	 * @param path File path to the image.
	 * @return Decoded image with width, height, and pixel data.
	 */
	static DecodedImage DecodeImageRGBA8_ThreadSafe(const std::wstring& path);

	/**
	 * @brief Checks if the image at the given path has an alpha channel.
	 * @param path File path to the image.
	 * @return True if the image has an alpha channel, false otherwise.
	 */
	static bool HasAlphaChannel(const std::wstring& path);
};

