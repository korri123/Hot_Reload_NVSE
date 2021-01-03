#include "ScriptCommands.h"
#include "GameScript.h"

UnorderedSet<UInt32> g_gameHotLoadedScripts;


bool Cmd_GetGameHotReloaded_Execute(COMMAND_ARGS)
{
	if (scriptObj && g_gameHotLoadedScripts.Erase(scriptObj->refID))
		*result = 1;
	else *result = 0;

	return true;
}