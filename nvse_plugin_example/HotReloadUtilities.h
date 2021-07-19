#pragma once
#include "Utilities.h"
#include <string>

inline std::string GetCurPath()
{
	char buffer[MAX_PATH] = { 0 };
    GetModuleFileName( NULL, buffer, MAX_PATH );
    std::string::size_type pos = std::string(buffer).find_last_of("\\/");
    return std::string(buffer).substr(0, pos);
}

std::string GetScriptsDir();

inline void Log(const std::string& s, bool warn=false)
{
	auto str = "HOT RELOAD: " + s;
	GeckExtenderMessageLog(str.c_str());
	if (warn)
		ShowErrorMessageBox(str.c_str());
}

inline bool ValidString(const char* str)
{
	return str && strlen(str);
}