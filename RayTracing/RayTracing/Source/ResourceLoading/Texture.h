#pragma once

enum class TextureType
{
	Albedo,
	Normal,
	Metalness,
	Roughness,
	Emissive,
};


/**
 * @brief Represents a texture resource with its type and file path.
 */
struct Texture
{
	/**< Type of this texture in material usage (e.g., "texture_diffuse", "texture_specular"). */
	TextureType mType;
	/**< Original file path or identifier for the texture resource. */
	std::string mPath;

	Texture(TextureType type, std::string path)
		: mType(type), mPath(std::move(path))
	{}
};


