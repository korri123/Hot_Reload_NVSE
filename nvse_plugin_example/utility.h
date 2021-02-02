#pragma once
#include <string>

inline std::string GetCurPath()
{
	char path[MAX_PATH];
	GetCurrentDirectory(MAX_PATH, path);
	return path;
}

inline std::string GetScriptsDir()
{
	return GetCurPath() + "\\Data\\Scripts";
}