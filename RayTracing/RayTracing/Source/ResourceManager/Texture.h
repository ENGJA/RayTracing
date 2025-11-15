#pragma once
struct Texture
{
	/**< Type of this texture in material usage (e.g., "texture_diffuse", "texture_specular"). */
	std::string mType;
	/**< Original file path or identifier for the texture resource. */
	std::string mPath;
};

