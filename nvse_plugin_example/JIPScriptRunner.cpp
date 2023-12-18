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
std::unordered_map<std::filesystem::path, std::string> g_fileContents;
//	Script() { ThisStdCall(0x5AA0F0, this); }
// ~Script() { ThisStdCall(0x5AA1A0, this); }
bool ScriptRecompile(const char* scrName, const char* text, Script* script)
{
	const auto newScript = MakeUnique<Script, 0x5AA0F0, 0x5AA1A0>();
	ScriptBuffer scrBuffer;
	scrBuffer.scriptText = text;
	scrBuffer.runtimeMode = ScriptBuffer::kGameConsole;
	scrBuffer.scriptName.Set(scrName);
	scrBuffer.partialScript = true;
	scrBuffer.currentScript = script;
	const auto result = StdCall<bool>(0x5AEB90, newScript.get(), &scrBuffer);
	if (!result)
		return false;
	FormHeap_Free(script->data);
	script->data = newScript->data;
	newScript->data = nullptr;
	script->varList.Replace(&newScript->varList);
	script->refList.Replace(&newScript->refList);

	return true;
}

void ScriptRun(Script* script)
{
	ThisStdCall(0x5AC1E0, script, nullptr, nullptr, nullptr, true);
}

std::string ReadFile(const std::filesystem::path& path)
{
	std::ifstream t(path);
	std::stringstream buffer;
	buffer << t.rdbuf();
	return buffer.str();
}

tList<VariableInfo>* CreateVarListCopy(tList<VariableInfo>* varList)
{
	auto* newVarList = New<tList<VariableInfo>>();
	CdeclCall(0x5AB930, varList, newVarList);
	return newVarList;
}

struct FindNextChange
{
	HANDLE handle;
	FindNextChange(HANDLE handle) : handle(handle) {}
	~FindNextChange()
	{
		FindNextChangeNotification(handle);
	}
};

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

		FindNextChange change(handle);
		std::vector<std::pair<std::string, std::string>> scriptNames;
		for (std::filesystem::recursive_directory_iterator next(folder), end; next != end; ++next)
		{
			auto path = next->path();
			auto extension = path.extension().string();
			auto fileName = path.filename().string();
			if (extension != ".txt")
				continue;
			auto source = ReadFile(path);
			if (auto iter = g_fileContents.find(path); iter != g_fileContents.end() && iter->second == source)
				continue;
			g_fileContents[path] = source;
			
			if (source.empty())
				continue;
			source = ReplaceAll(source, "\r\n", "\n"); // lol
			source = ReplaceAll(source, "\n", "\r\n");

			scriptNames.emplace_back(fileName, source);
		}

		if (scriptNames.empty())
			continue;

		ScopedLock lock(g_criticalSection);
		g_mainThreadExecutionQueue.push([=]
		{
			for (auto& [name, source] : scriptNames)
			{
				auto* form = CdeclCall<TESForm*>(0x483A00, name.c_str());
				if (!form)
				{
					continue;
				}
				auto* script = DYNAMIC_CAST(form, TESForm, Script);
				if (!script)
				{
					Log(FormatString("Failed to cast form %s to script", name.c_str()));
					continue;
				}


				const auto result = ScriptRecompile(name.c_str(), source.c_str(), script);
				if (result)
				{
					HandleHotReloadSideEffects(script, "JIP Script Runner");
					if (g_runJipScriptRunner)
					{
						ScriptRun(script);
					}
				}
				
			}
		});
	}
}

void PopulateSources(const std::string& dir)
{
	for (std::filesystem::recursive_directory_iterator next(dir), end; next != end; ++next)
	{
		const auto path = next->path();
		const auto extension = path.extension().string();
		if (extension != ".txt")
			continue;
		const auto source = ReadFile(path);
		g_fileContents.emplace(path, source);
	}
}

void StartScriptRunnerWatchThread()
{
	std::string scriptsFolder = GetCurPath() + R"(\Data\NVSE\Plugins\scripts)";
	if (!g_altScriptRunnerPath.empty())
		scriptsFolder = GetCurPath() + '\\' + g_altScriptRunnerPath;
	if (!std::filesystem::exists(scriptsFolder))
	{
		Log("No scripts folder found, Hot Reload will not watch JIP script runner files, looked in path: " + scriptsFolder);
		return;
	}
	PopulateSources(scriptsFolder);

	g_scriptRunnerWatchThread = std::thread(WatchScriptRunner, scriptsFolder);
	g_scriptRunnerWatchThread.detach();
}

