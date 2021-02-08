#include "HotReloadUtilities.h"

extern std::string g_scriptsFolder;

std::string GetScriptsDir()
{
	return GetCurPath() + g_scriptsFolder;
}
