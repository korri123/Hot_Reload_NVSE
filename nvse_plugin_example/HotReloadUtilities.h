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
#if RUNTIME
	_MESSAGE("%s", str.c_str());
	Console_Print_Long(str);
#else
	GeckExtenderMessageLog(str.c_str());
	if (warn)
		ShowErrorMessageBox(str.c_str());
#endif
}

inline bool ValidString(const char* str)
{
	return str && strlen(str);
}

template <typename T, const UInt32 ConstructorPtr = 0, typename... Args>
T* New(Args &&... args)
{
	auto* alloc = FormHeap_Allocate(sizeof(T));
	if constexpr (ConstructorPtr)
	{
		ThisStdCall(ConstructorPtr, alloc, std::forward<Args>(args)...);
	}
	else
	{
		memset(alloc, 0, sizeof(T));
	}
	return static_cast<T*>(alloc);
}

template <typename T, const UInt32 DestructorPtr = 0, typename... Args>
void Delete(T* t, Args &&... args)
{
	if constexpr (DestructorPtr)
	{
		ThisStdCall(DestructorPtr, t, std::forward<Args>(args)...);
	}
	FormHeap_Free(t);
}