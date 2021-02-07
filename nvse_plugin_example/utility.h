#pragma once
#include "Utilities.h"
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

inline void Log(const std::string& s, bool warn=false)
{
	auto str = "HOT RELOAD: " + s;
	GeckExtenderMessageLog(str.c_str());
	if (warn)
		ShowErrorMessageBox(str.c_str());
}