#include <functional>
#include <queue>

#include "SocketUtils.h"
#include "GameAPI.h"
#include "GameRTTI.h"
#include "SafeWrite.h"
#include <thread>
#include <utility>
#include <unordered_set>

#include "GameData.h"
#include "HotReload.h"
#include "ScriptTokenCache.h"
#include "ScriptCommands.h"

extern NVSEDataInterface* g_dataInterface;
extern void (*ClearLambdasForScript)(Script*);

class VarInfoObject : public VarInfoTransferObject
{
public:
	VarInfoObject(const VarInfoTransferObject& obj, std::string name) : VarInfoTransferObject(obj),
	                                                                    name(std::move(name))
	{
	}

	std::string name;
};

class RefInfoObject : public RefInfoTransferObject
{
public:
	RefInfoObject(const RefInfoTransferObject& obj, std::string name, std::string esmName) : RefInfoTransferObject(obj),
	                                                                                         name(std::move(name)),
	                                                                                         esmName(std::move(esmName))
	{
	}

	std::string name;
	std::string esmName;
};

std::thread g_ReloadThread;

void HotReloadConsolePrint(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	char msg[0x400];
	vsprintf_s(msg, 0x400, fmt, args);
	Console_Print("HOT RELOAD: %s", msg);
}

void Error(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	char errorMsg[0x400];
	vsprintf_s(errorMsg, 0x400, fmt, args);

	_MESSAGE("Hot reload error: %s", errorMsg);
	HotReloadConsolePrint("Error - %s", errorMsg);
	QueueUIMessage("Hot reload error (see console print)", 0, reinterpret_cast<const char*>(0x1049638), nullptr, 2.5F,
	               false);
}

void QueueErrorMessage(const char* fmt, ...)
{
	va_list args;
	va_start(args, fmt);

	char errorMsg[0x400];
	vsprintf_s(errorMsg, 0x400, fmt, args);
	ScopedLock lock(g_criticalSection);
	g_mainThreadExecutionQueue.push([=]() { Error(errorMsg); });
}

bool HandleScriptVarChanges(Script* script, const std::vector<VarInfoObject>& vec)
{
	tList<VariableInfo> newVars{};
	for (const auto& editorVar : vec)
	{
		auto* newVar = static_cast<VariableInfo*>(FormHeap_Allocate(sizeof(VariableInfo)));
		auto* gameVar = script->varList.GetVariableByName(editorVar.name.c_str());
		newVar->idx = editorVar.idx;
		if (gameVar)
			newVar->data = gameVar->data;
		else
			newVar->data = 0;
		newVar->type = editorVar.type;
		newVar->name = String();
		newVar->name.Set(editorVar.name.c_str());
		newVars.Append(newVar);
	}
	auto* oldVars = reinterpret_cast<tList<VariableInfo>*>(&script->varList);
	// oldVars->DeleteAll();
	*oldVars = newVars;
	return true;
}

bool HandleRefListChanges(Script* script, const std::vector<RefInfoObject>& vec)
{
	tList<Script::RefVariable> newRefs{};
	for (const auto& editorRef : vec)
	{
		auto* ref = static_cast<Script::RefVariable*>(FormHeap_Allocate(sizeof(Script::RefVariable)));
		if (editorRef.formId && !editorRef.varIdx)
		{
			UInt32 refId;
			if (!editorRef.esmName.empty())
			{
				const auto* modInfo = DataHandler::Get()->LookupModByName(editorRef.esmName.c_str());
				if (!modInfo)
				{
					Error("Reloaded script has a reference to ESM not loaded in game (%s)", editorRef.esmName.c_str());
					return false;
				}

				refId = editorRef.formId + (modInfo->modIndex << 24);
			}
			else
			{
				// default ref?
				refId = editorRef.formId;
			}

			auto* form = LookupFormByID(refId);
			if (!form)
			{
				Error("Reloaded script has a reference to form not loaded in game (%s)", editorRef.name.c_str());
				return false;
			}
			ref->form = form;
			ref->varIdx = 0;
		}
		else
		{
			// refVar
			ref->varIdx = editorRef.varIdx;
			ref->form = nullptr;
		}
		ref->name = String();
		ref->name.Set(editorRef.name.c_str());
		newRefs.Append(ref);
	}
	auto* refList = reinterpret_cast<tList<Script::RefVariable>*>(&script->refList);
	refList->DeleteAll();
	*refList = newRefs;
	return true;
}

class ReloadedScript
{
public:
	tList<VariableInfo>* oldVarList;

	ReloadedScript(tList<VariableInfo>* oldVarList) : oldVarList(oldVarList)
	{
	}

	~ReloadedScript()
	{
		oldVarList->DeleteAll();
		FormHeap_Free(oldVarList);
	}
};

std::unordered_map<UInt32, ReloadedScript> g_reloadedScripts;
std::unordered_set<ScriptEventList*> g_handledEventLists;

tList<VariableInfo>* GetVarList(Script* script)
{
	auto* newList = static_cast<tList<VariableInfo>*>(FormHeap_Allocate(sizeof(tList<VariableInfo>)));
	*newList = *reinterpret_cast<tList<VariableInfo>*>(&script->varList);
	return newList;
}

VariableInfo* GetVariableInfo(tList<VariableInfo>& list, UInt32 varIdx)
{
	auto* node = list.Head();
	while (node != nullptr)
	{
		if (node->Data() && node->Data()->idx == varIdx)
			return node->Data();
		node = node->Next();
	}
	return nullptr;
}

std::queue<std::function<void()>> g_mainThreadExecutionQueue;
ICriticalSection g_criticalSection;

void HandleHotReloadSideEffects(Script* script, const std::string& modName)
{
	
	HotReloadConsolePrint("Reloaded script '%s' in '%s'", script->GetName(), modName.c_str());
	const auto queuedMsg = "Hot reloaded " + std::string(script->GetName());
	QueueUIMessage(queuedMsg.c_str(), 0, nullptr, nullptr, 2.5F, false);
	g_handledEventLists.clear();
	g_dataInterface->ClearScriptDataCache();
	ClearLambdasForScript(script);
	g_gameHotLoadedScripts.Insert(script->refID);
}


void HandleHotReload()
{
	
	SocketServer hotReloadServer(g_nvsePort);
	hotReloadServer.WaitForConnection();
	ScriptTransferObject obj{};
	hotReloadServer.ReadData(obj);
	std::string modName;
	hotReloadServer.ReadData(modName, obj.nameLength);
	std::vector<char> scriptData(obj.dataLength, 0);
	hotReloadServer.ReadData(scriptData.data(), obj.dataLength);
	std::vector<VarInfoObject> varInfos;
	for (auto i = 0U; i < obj.numVars; ++i)
	{
		VarInfoTransferObject varInfo;
		hotReloadServer.ReadData(varInfo);
		std::string varName;
		hotReloadServer.ReadData(varName, varInfo.nameLength);
		varInfos.emplace_back(varInfo, varName);
	}
	std::vector<RefInfoObject> refInfos;
	for (auto i = 0U; i < obj.numRefs; ++i)
	{
		RefInfoTransferObject refInfo;
		hotReloadServer.ReadData(refInfo);
		std::string refName;
		hotReloadServer.ReadData(refName, refInfo.nameLength);
		std::string esmName;
		hotReloadServer.ReadData(esmName, refInfo.esmNameLength);
		refInfos.emplace_back(refInfo, refName, esmName);
	}
	hotReloadServer.CloseConnection();
	const auto* modInfo = DataHandler::Get()->LookupModByName(modName.c_str());
	if (!modInfo)
	{
		QueueErrorMessage("Mod name %s is not loaded in-game", modName.c_str());
		return;
	}
	const auto refId = obj.scriptRefID + (modInfo->modIndex << 24);
	auto* form = LookupFormByID(refId);
	if (!form)
	{
		QueueErrorMessage("Reloading new scripts is not supported. (Script form ID %X is not loaded in game)", refId);
		return;
	}
	auto* script = DYNAMIC_CAST(form, TESForm, Script);
	if (!script)
	{
		QueueErrorMessage("Tried to hot reload an invalid script");
		return;
	}


	ScopedLock lock(g_criticalSection);
	// avoid concurrency issues as the server is running on a different thread
	g_mainThreadExecutionQueue.push([=]()
	{
		auto* oldVarList = GetVarList(script);
		if (!HandleRefListChanges(script, refInfos))
			return;
		if (!HandleScriptVarChanges(script, varInfos))
			return;

		auto* oldDataPtr = script->data;
		auto* newData = FormHeap_Allocate(obj.dataLength);
		std::memcpy(newData, scriptData.data(), obj.dataLength);
		script->data = newData;
		FormHeap_Free(oldDataPtr);
		script->info.dataLength = obj.dataLength;
		script->info.varCount = obj.numVars;
		script->info.numRefs = obj.numRefs;
		const auto reloadedScriptIter = g_reloadedScripts.find(script->refID);
		if (reloadedScriptIter != g_reloadedScripts.end())
			g_reloadedScripts.erase(reloadedScriptIter);
		g_reloadedScripts.emplace(std::make_pair(script->refID, oldVarList));
		HandleHotReloadSideEffects(script, modName);
	});
	
}


void InitHotReloadServer(int i)
{
	try
	{
		while (true)
		{
			HandleHotReload();
		}
	}
	catch (const SocketException& e)
	{
		QueueErrorMessage("Encountered error in Hot Reload server, shutting down... %s", e.what());
	}
}

void __fastcall HandleScriptEventListChange(ScriptRunner* runner, Script* script)
{
	const auto reloadScriptIter = g_reloadedScripts.find(script->refID);
	if (reloadScriptIter == g_reloadedScripts.end())
		return;
	if (g_handledEventLists.find(runner->eventList) != g_handledEventLists.end())
		return;
	auto* newEventListVars = static_cast<tList<ScriptEventList::Var>*>(FormHeap_Allocate(
		sizeof(tList<ScriptEventList::Var>)));
	*newEventListVars = tList<ScriptEventList::Var>();
	auto& oldScriptVarInfos = *reloadScriptIter->second.oldVarList;
	auto& newScriptVarInfos = *reinterpret_cast<tList<VariableInfo>*>(&script->varList);
	for (auto iter = newScriptVarInfos.Begin(); !iter.End(); ++iter)
	{
		auto* newEventListVar = static_cast<ScriptEventList::Var*>(FormHeap_Allocate(sizeof(ScriptEventList::Var)));
		newEventListVar->id = iter->idx;
		newEventListVar->nextEntry = nullptr;
		newEventListVar->data = 0;
		auto* oldVarInfo = GetVariableInfo(oldScriptVarInfos, iter->idx);
		if (oldVarInfo)
		{
			auto* varData = runner->eventList->GetVariable(oldVarInfo->idx);
			if (varData)
			{
				newEventListVar->data = varData->data;
			}
		}
		else
		{
			HotReloadConsolePrint("Initialized new variable '%s' >> 0", iter->name.CStr());
		}
		auto* nextLastNode = newEventListVars->GetLastNode();
		newEventListVars->Append(newEventListVar);
		auto* newLastNode = newEventListVars->GetLastNode();
		if (nextLastNode != newLastNode)
			nextLastNode->Data()->nextEntry = reinterpret_cast<ScriptEventList::VarEntry*>(newLastNode);
	}
	auto* oldEventListVars = reinterpret_cast<tList<ScriptEventList::Var>*>(runner->eventList->m_vars);
	oldEventListVars->DeleteAll();
	FormHeap_Free(oldEventListVars);
	runner->eventList->m_vars = reinterpret_cast<ScriptEventList::VarEntry*>(newEventListVars);
	g_handledEventLists.insert(runner->eventList);
}

__declspec(naked) void Hook_HandleScriptEventListChange()
{
	static UInt32 returnAddress = 0x5E158F;
	static UInt32 hookedCall = 0x671D10;
	__asm
		{
		call hookedCall
		push eax
		mov ecx, [ebp - 0xED0]
		mov edx, [ebp + 0x8]
		call HandleScriptEventListChange
		pop eax
		jmp returnAddress
		}
}

void InitializeHotReloadRuntime()
{
	WriteRelJump(0x5E158A, UInt32(Hook_HandleScriptEventListChange));
	g_ReloadThread = std::thread(InitHotReloadServer, 0);
	g_ReloadThread.detach();
}
