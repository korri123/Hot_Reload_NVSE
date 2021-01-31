#pragma once
#include <CommandTable.h>
#include "ParamInfos.h"

#if RUNTIME
extern UnorderedSet<UInt32> g_gameHotLoadedScripts;
#endif

DEFINE_COMMAND_PLUGIN(GetGameHotReloaded, "Returns true if script was hot reloaded", 0, 0, nullptr)
DEFINE_COMMAND_PLUGIN(ToGeck, "Sends selected object to GECK", false, 1, kParams_OneOptionalForm)

bool Cmd_GetGameHotReloaded_Execute(COMMAND_ARGS);

bool Cmd_ToGeck_Execute(COMMAND_ARGS);
