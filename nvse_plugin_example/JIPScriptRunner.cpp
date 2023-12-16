#include "JIPScriptRunner.h"

#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#include "HotReloadUtilities.h"

#include <thread>

#include "GameAPI.h"
#include "GameData.h"
#include "GameRTTI.h"
#include "HotReload.h"

std::thread g_scriptRunnerWatchThread;
std::unordered_map<std::filesystem::path, std::filesystem::file_time_type> g_lastWriteTimes;

bool ScriptRecompile(const char* scrName, const char* text, Script* script)
{
	ScriptBuffer scrBuffer;
	scrBuffer.scriptText = text;
	scrBuffer.runtimeMode = ScriptBuffer::kGameConsole;
	scrBuffer.scriptName.Set(scrName);
	scrBuffer.partialScript = true;
	scrBuffer.currentScript = script;
	return StdCall<bool>(0x5AEB90, script, &scrBuffer);
}

void ScriptRun(Script* script)
{
	ThisStdCall(0x5AC1E0, script, nullptr, nullptr, nullptr, true);
}

tList<VariableInfo>* CreateVarListCopy(tList<VariableInfo>* varList)
{
	auto* newVarList = New<tList<VariableInfo>>();
	CdeclCall(0x5AB930, varList, newVarList);
	return newVarList;
}

void WatchScriptRunner(const std::string& folder)
{
	auto* handle = FindFirstChangeNotification(folder.c_str(), true, FILE_NOTIFY_CHANGE_LAST_WRITE);
	if (handle == INVALID_HANDLE_VALUE || !handle)
		throw std::runtime_error(FormatString("Could not find directory %s", GetScriptsDir().c_str()));
	while (true)
	{
		auto waitStatus = WaitForSingleObject(handle, INFINITE);
		if (waitStatus == WAIT_FAILED)
			throw std::runtime_error("Failed to wait");

		std::map<Script*, std::filesystem::path> queuedScripts;
		for (std::filesystem::recursive_directory_iterator next(folder), end; next != end; ++next)
		{
			auto extension = next->path().extension().string();
			auto fileName = next->path().filename().string();
			if (extension != ".txt")
				continue;
			auto lastWriteTime = last_write_time(next->path());
			if (auto iter = g_lastWriteTimes.find(next->path()); iter != g_lastWriteTimes.end() && iter->second == lastWriteTime)
				continue;
			g_lastWriteTimes[next->path()] = lastWriteTime;
			auto* form = CdeclCall<TESForm*>(0x483A00, fileName.c_str());
			if (!form)
			{
				Log("Failed to find form for script %s", fileName.c_str());
				continue;
			}

			if (form)
			{
				auto* script = DYNAMIC_CAST(form, TESForm, Script);
				if (script)
				{
					queuedScripts.emplace(script, next->path());
				}
			}
		}

		g_mainThreadExecutionQueue.push([=]
		{
			for (auto& entry : queuedScripts)
			{
				auto& path = entry.second;
				auto* script = entry.first;
				std::ifstream t(path);
				std::stringstream buffer;
				buffer << t.rdbuf();
				auto str = buffer.str();
				if (!str.empty())
				{
					str = ReplaceAll(str, "\r\n", "\n"); // lol
					str = ReplaceAll(str, "\n", "\r\n");
					
					const auto scriptName = path.filename().string();
					auto* oldVarList = CreateVarListCopy(reinterpret_cast<tList<VariableInfo>*>(&script->varList));
					const auto result = ScriptRecompile(scriptName.c_str(), str.c_str(), script);
					if (result)
					{
						HandleHotReloadSideEffects(script, oldVarList, "JIP Script Runner");
						Log(FormatString("Compiled script '%s' from path '%s'", scriptName.c_str(), path.string().c_str()));
						if (g_runJipScriptRunner)
						{
							ScriptRun(script);
						}
					}
					else
					{
						Delete(oldVarList);
					}
				}
			}
		});
		
		if (!FindNextChangeNotification(handle))
			throw std::runtime_error("Failed to read again handle.");
	}
}

void PopulateLastWriteTimes(const std::string& dir)
{
	for (std::filesystem::recursive_directory_iterator next(dir), end; next != end; ++next)
	{
		auto extension = next->path().extension().string();
		if (extension != ".txt")
			continue;
		auto lastWriteTime = last_write_time(next->path());
		g_lastWriteTimes.emplace(next->path(), lastWriteTime);
	}
}

void StartScriptRunnerWatchThread()
{
	std::string scriptsFolder = GetCurPath() + "\\Data\\NVSE\\Plugins\\scripts";

	if (!std::filesystem::exists(scriptsFolder))
	{
		Log("No scripts folder found, Hot Reload will not watch JIP script runner files");
		return;
	}

	g_scriptRunnerWatchThread = std::thread(WatchScriptRunner, scriptsFolder);
	g_scriptRunnerWatchThread.detach();
}

