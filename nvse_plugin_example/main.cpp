#include "CreateScriptFiles.h"
#include "HotReload.h"
#include "OpenInGeck.h"
#include "nvse/PluginAPI.h"
#include "PluginAPI.h"
#include "SafeWrite.h"
#include "ScriptCommands.h"
#include "SimpleINILibrary.h"
#if EDITOR
#include "CompileScriptFromFile.h"
#endif

#if RUNTIME
IDebugLog gLog("hot_reload.log");
NVSEDataInterface *g_dataInterface;
#else
IDebugLog gLog("hot_reload_editor.log");
#endif
PluginHandle g_pluginHandle = kPluginHandle_Invalid;

#if RUNTIME
void MessageHandler(NVSEMessagingInterface::Message *msg)
{

	if (msg->type == NVSEMessagingInterface::kMessage_MainGameLoop)
	{
		ScopedLock lock(g_criticalSection);
		while (!g_mainThreadExecutionQueue.empty())
		{
			const auto &callback = g_mainThreadExecutionQueue.back();
			callback();
			g_mainThreadExecutionQueue.pop();
		}
	}
}
#endif

bool NVSEPlugin_Query(const NVSEInterface *nvse, PluginInfo *info)
{
	info->infoVersion = PluginInfo::kInfoVersion;
	info->name = "hot_reload";
	info->version = 2;

	// version checks
	if (nvse->nvseVersion < PACKED_NVSE_VERSION)
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
#define RegisterScriptCommand(name) nvse->RegisterCommand(&kCommandInfo_##name);
#endif

void PatchLockFiles()
{
	UInt8 pushInstr[] = {0x6a, 0x07, 0x90};
#if RUNTIME
	SafeWriteBuf(0xEE3343, pushInstr, sizeof(pushInstr) / sizeof(UInt8));
#else
	SafeWriteBuf(0xC7DFCF, pushInstr, sizeof(pushInstr) / sizeof(UInt8));
#endif
}

bool g_enableCreateFiles = true;
std::string g_createFileExtension = "gek";
std::string g_scriptsFolder = "\\Scripts";
bool g_saveFileWhenScriptSaved = true;
bool g_openScriptsFolder = true;
bool g_enableTextEditor = true;
#if RUNTIME
void (*ClearLambdasForScript)(Script *) = nullptr;
#endif

void CrtErrorHandler(const wchar_t *expression, const wchar_t *function, const wchar_t *file, unsigned int line, uintptr_t pReserved)
{
	Log("Critical error, please create a bug report on Nexus describing how this happened, with a download link to the .esp/.esm you are editing.", true);
}

bool NVSEPlugin_Load(const NVSEInterface *nvse)
{
#if EDITOR
	_set_invalid_parameter_handler(CrtErrorHandler);
	CoInitializeEx(NULL, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
#endif
	const auto iniPath = GetCurPath() + R"(\Data\NVSE\Plugins\hot_reload.ini)";

	CSimpleIniA ini;
	ini.SetUnicode();
	const auto errVal = ini.LoadFile(iniPath.c_str());

	const auto enableHotReload = ini.GetOrCreate("General", "bHotReload", 1, "; Enable script hot reload");
	const auto g_enableTextEditor = ini.GetOrCreate("General", "bTextEditorSupport", 1, "; Enable external text editor compile-on-save support for scripts");
	const auto enableToGeck = ini.GetOrCreate("General", "bToGeck", 1, "; Enable ToGECK command that allows you to quickly send a ref or form to GECK from console");
	auto enableSaveWhileGameOpen = ini.GetOrCreate("General", "bAllowSavingWhileGameIsOpen", 1, "; Allow GECK to save files while game is open");
	g_enableCreateFiles = ini.GetOrCreate("General", "bSynchronizeScriptsWithFiles", 1, "; Create text files inside Scripts\\ folder for every script of a mod once you save a script in that mod and updates the scripts in file when you edit them in GECK.\n; Enables automatic synchronization between GECK and script files.");
	g_createFileExtension = ini.GetOrCreate("General", "sCreateFilesFileExtension", "gek", "; File extension of automatically generated files from scripts inside mod");
	g_scriptsFolder = ini.GetOrCreate("General", "sScriptsFolderPath", "Scripts", "; Path to Scripts folder (relative to base Fallout New Vegas directory) used for text editor feature");
	auto forceAllowUnsafeSave = ini.GetOrCreate("General", "bForceAllowWindowSave", 1, "; Force GECK to allow saving while script window and other dialog boxes are open");
	g_saveFileWhenScriptSaved = ini.GetOrCreate("General", "bSaveFileOnScriptCompile", 1, "; Save the loaded esp/esm each time you save/compile a script (requires bAllowSavingWhileGameIsOpen and bForceAllowWindowSave to be 1)");
	g_openScriptsFolder = ini.GetOrCreate("General", "bOpenScriptFolder", 1, "; Open the folder containing the scripts of the loaded esp/esm when opening a mod in GECK");

	if (g_saveFileWhenScriptSaved)
	{
		forceAllowUnsafeSave = true;
		enableSaveWhileGameOpen = true;
	}

	g_scriptsFolder = ReplaceAll(g_scriptsFolder, "/", "\\");
	if (g_scriptsFolder.empty())
		g_scriptsFolder = "\\Scripts";
	if (g_scriptsFolder.at(0) != '\\')
		g_scriptsFolder = '\\' + g_scriptsFolder;

	ini.SaveFile(iniPath.c_str(), false);

#if RUNTIME
	if (nvse->isEditor)
		return true;
	g_dataInterface = static_cast<NVSEDataInterface *>(nvse->QueryInterface(kInterface_Data));
	ClearLambdasForScript = (void (*)(Script *))(g_dataInterface->GetFunc(NVSEDataInterface::kNVSEData_LambdaDeleteAllForScript));
	if (!ClearLambdasForScript)
	{
		_ERROR("NVSE version either outdated or missing kNVSEData_LambdaDeleteAllForScript");
		return false;
	}
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
	if (enableHotReload)
		RegisterScriptCommand(GetGameHotReloaded)

	if (enableToGeck)
		RegisterScriptCommand(ToGeck)

#if RUNTIME
					auto *messagingInterface = static_cast<NVSEMessagingInterface *>(nvse->QueryInterface(kInterface_Messaging));
	messagingInterface->RegisterListener(g_pluginHandle, "NVSE", MessageHandler);

	if (enableHotReload)
		InitializeHotReloadRuntime();

		//SafeWrite32(0xEC9A89 + 1, 0x30); // change from exclusive access to write access

#else
					if (enableHotReload)
						InitializeHotReloadEditor();
	if (enableToGeck)
		StartGeckServer();
	if (forceAllowUnsafeSave)
	{
		const auto *patch = "\xEB\x31\x90\x90\x90"; // jmp 0x444E3F
		SafeWriteBuf(0x444E0C, (void *)patch, strlen(patch));
	}
	PatchPostPluginLoad();

#endif
	if (enableSaveWhileGameOpen)
		PatchLockFiles();
	_MESSAGE("Successfully loaded");
	return true;
}
