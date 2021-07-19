#include "SocketUtils.h"
#include "GameAPI.h"
#include "SafeWrite.h"
#include <thread>

#include "GameData.h"
#include "HotReload.h"
#include <filesystem>

#include "CreateScriptFiles.h"
#include <intrin.h>

typedef void (__cdecl* _EditorLog)(ScriptBuffer* Buffer, const char* format, ...);
const _EditorLog EditorLog = reinterpret_cast<_EditorLog>(0x5C5730);

HWND__* g_hwnd = nullptr;

HWND__* GetGeckWindow()
{
	if (!g_hwnd)
	{
		auto* window = FindWindow("Garden of Eden Creation Kit", nullptr);
		if (!window)
		{
			ShowErrorMessageBox("Failed to find GECK window!");
			return nullptr;
		}
		g_hwnd = window;
	}
	return g_hwnd;
}

void DoSendHotReloadData(Script* script)
{
	const char* esmFileName;
	if (script->mods.Head() && script->mods.Head()->data && ValidString(script->mods.Head()->data->name))
	{
		esmFileName = script->mods.Head()->data->name;
	}
	else if (DataHandler::Get()->activeFile && ValidString(DataHandler::Get()->activeFile->name))
	{
		esmFileName = DataHandler::Get()->activeFile->name;
	}
	else
	{
		GeckExtenderMessageLog("Failed to get name of mod");
		return;
	}
	SocketClient client("127.0.0.1", g_nvsePort);
	ScriptTransferObject scriptTransferObject;
	scriptTransferObject.scriptRefID = script->refID & 0x00FFFFFF;
	scriptTransferObject.dataLength = script->info.dataLength;
	scriptTransferObject.nameLength = strlen(esmFileName);
	scriptTransferObject.numVars = script->GetVarCount();
	scriptTransferObject.numRefs = script->GetRefCount();
	scriptTransferObject.type = script->info.type;
	client.SendData(scriptTransferObject);
	client.SendData(esmFileName, scriptTransferObject.nameLength);
	client.SendData(static_cast<char*>(script->data), script->info.dataLength);
	auto* varNode = &script->varList;
	while (varNode)
	{
		if (varNode->data)
		{
			VarInfoTransferObject obj(varNode->data->idx, varNode->data->type, varNode->data->name.m_dataLen);
			client.SendData(obj);
			client.SendData(varNode->data->name.CStr(), varNode->data->name.m_dataLen);
		}
		varNode = varNode->Next();
	}
	auto* refNode = &script->refList;
	while (refNode)
	{
		auto* data = refNode->var;
		if (data)
		{
			const auto* esmName = data->form ? data->form->mods.Head()->data ? data->form->mods.Head()->data->name : nullptr : nullptr;
			RefInfoTransferObject refObj(data->name.m_dataLen, data->form ? data->form->refID & 0x00FFFFFF : 0, esmName ? strlen(esmName) : 0, data->varIdx);
			client.SendData(refObj);
			client.SendData(data->name.CStr(), refNode->var->name.m_dataLen);
			if (esmName)
				client.SendData(esmName, strlen(esmName));
		}
		refNode = refNode->Next();
	}
}
extern bool g_saveFileWhenScriptSaved;


void SendHotReloadData(Script* script)
{
	if (!script)
	{
		Log("Script was null!");
		return;
	}
	try
	{
		DoSendHotReloadData(script);
		const auto* scriptName = script->editorData.editorID.CStr();
		if (ValidString(scriptName))
		{
			Log(FormatString("Hotloaded script '%s'", scriptName));
		}
		else
		{
			Log("Hotloaded script");
		}
	}
	catch (const SocketException& e)
	{
		_MESSAGE("Hot reload error: %s", e.what());
		if (e.m_errno != 10061) // game isn't open
			GeckExtenderMessageLog("Hot reload error: %s", e.what());
	}
	catch (...)
	{
		Log("Critical error in HotReloadEditor.cpp, please open a bug report on how this happened", true);
	}
	

}
extern std::atomic<bool> g_compilingFromFile;
std::thread g_hotReloadClientThread;
void __fastcall SendHotReloadDataHook(Script* script)
{
	if (LookupFormByID(script->refID) && script->text && _stricmp(script->editorData.editorID.CStr(), "DefaultCompiler") != 0) // ignore temp scripts
	{
		g_hotReloadClientThread = std::thread(SendHotReloadData, script);
		g_hotReloadClientThread.detach();
		if (!g_compilingFromFile)
			CreateScriptFiles();
	}
	// do in main thread
	if (g_saveFileWhenScriptSaved)
	{
		// run after * appears
		auto saveThread = std::thread([]()
		{
			try
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(50));
				if (auto* window = GetGeckWindow())
				{
					if (DataHandler::Get()->activeFile)
						SendMessage(window, WM_COMMAND, 0x9CD2, reinterpret_cast<LPARAM>(DataHandler::Get()->activeFile->name)); //save
				}
			}
			catch (...)
			{
				Log("Critical error in HotReloadEditor.cpp (save after compile), please open a bug report on how this happened", true);
			}
		});
		saveThread.detach();
	}
}

__declspec(naked) void Hook_HotReload()
{
	static UInt32 ScriptContext__CompileScript = 0x5C9800;
	static UInt32 returnLocation = 0x5C31C0;
	static Script* script = nullptr;
	__asm
	{
		mov [script], esi
		call ScriptContext__CompileScript
		test al, al
		jz goBack
		mov ecx, script
		push eax
		call SendHotReloadDataHook
		pop eax
	goBack:
		jmp returnLocation
	}
}

void InitializeHotReloadEditor()
{
	WriteRelJump(0x5C31BB, UInt32(Hook_HotReload));
	// Patch useless micro optimization that prevents ref variable names getting loaded
	PatchMemoryNop(0x5C5150, 6);
}