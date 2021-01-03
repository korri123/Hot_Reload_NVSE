#pragma once
#include <CommandTable.h>

#if RUNTIME
extern UnorderedSet<UInt32> g_gameHotLoadedScripts;
#endif

DEFINE_COMMAND_PLUGIN(GetGameHotReloaded, "prints a string", 0, 0, nullptr)

bool Cmd_GetGameHotReloaded_Execute(COMMAND_ARGS);
