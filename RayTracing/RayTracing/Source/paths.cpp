#include "pch.h"
#include "paths.h"
std::filesystem::path GetExecutableDirectory()
{
	return std::filesystem::path(__argv[0]).parent_path();
}

std::filesystem::path GetResourcePath(const std::string& relativePath)
{
	return GetExecutableDirectory().parent_path().parent_path() / "Resources" / relativePath;
}