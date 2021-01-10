#include "HotReload.h"
#include "OpenInGeck.h"
#include "nvse/PluginAPI.h"
#include "PluginAPI.h"
#include "ScriptCommands.h"

#if RUNTIME
IDebugLog gLog("hot_reload.log");
NVSEDataInterface* g_dataInterface;
#else
IDebugLog gLog("hot_reload_editor.log");
#endif
PluginHandle g_pluginHandle = kPluginHandle_Invalid;

#if RUNTIME
void MessageHandler(NVSEMessagingInterface::Message* msg)
{

	if (msg->type == NVSEMessagingInterface::kMessage_MainGameLoop)
	{
		ScopedLock lock(g_criticalSection);
		while (!g_mainThreadExecutionQueue.empty())
		{
			const auto& callback = g_mainThreadExecutionQueue.back();
			callback();
			g_mainThreadExecutionQueue.pop();
		}
	}

}
#endif

bool NVSEPlugin_Query(const NVSEInterface* nvse, PluginInfo* info)
{
	info->infoVersion = PluginInfo::kInfoVersion;
	info->name = "hot_reload";
	info->version = 2;

	// version checks
	if (nvse->nvseVersion < NVSE_VERSION_INTEGER)
	{
		const auto str = FormatString("HOT RELOAD: NVSE version too old (got %d expected at least %d). Plugin will NOT load! Install the latest version here: https://github.com/xNVSE/NVSE/releases/", nvse->nvseVersion, NVSE_VERSION_INTEGER);
		ShowErrorMessageBox(str.c_str());
		_ERROR(str.c_str());
		return false;
	}

	
	if (!nvse->isEditor)
	{
#if EDITOR
		return false;
#endif
		if (nvse->runtimeVersion < RUNTIME_VERSION_1_4_0_525)
		{
			_ERROR("incorrect runtime version (got %08X need at least %08X)", nvse->runtimeVersion, RUNTIME_VERSION_1_4_0_525);
			return false;
		}

		if (nvse->isNogore)
		{
			_ERROR("NoGore is not supported");
			return false;
		}
	}

	else
	{
#if RUNTIME
		return false;
#endif
		if (nvse->editorVersion < CS_VERSION_1_4_0_518)
		{
			_ERROR("incorrect editor version (got %08X need at least %08X)", nvse->editorVersion, CS_VERSION_1_4_0_518);
			return false;
		}
	}
	_MESSAGE("Successfully queried");
	return true;
}

#ifndef RegisterScriptCommand
#define RegisterScriptCommand(name) 	nvse->RegisterCommand(&kCommandInfo_ ##name);
#endif

bool NVSEPlugin_Load(const NVSEInterface* nvse)
{
#if RUNTIME
	if (nvse->isEditor)
		return true;
	g_dataInterface = static_cast<NVSEDataInterface*>(nvse->QueryInterface(kInterface_Data));
#else
	if (!nvse->isEditor)
		return true;
#endif
	WSADATA wsaData;
	const auto result = WSAStartup(MAKEWORD(2, 2), &wsaData);
	if (result != 0)
	{
		_ERROR("Failed to initialize WinSock 2");
		return false;
	}

	g_pluginHandle = nvse->GetPluginHandle();

	nvse->SetOpcodeBase(0x3911);
	RegisterScriptCommand(GetGameHotReloaded);
	RegisterScriptCommand(ToGeck);

#if RUNTIME
	auto* messagingInterface = static_cast<NVSEMessagingInterface*>(nvse->QueryInterface(kInterface_Messaging));
	messagingInterface->RegisterListener(g_pluginHandle, "NVSE", MessageHandler);
	InitializeHotReloadRuntime();
#else
	InitializeHotReloadEditor();
	StartGeckServer();
#endif
	_MESSAGE("Successfully loaded");
	return true;
}
