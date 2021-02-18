#include <algorithm>
#include <string>
#include <thread>
#include "Utilities.h"
#include "common/IDirectoryIterator.h"
#include "GameAPI.h"
#include "GameRTTI.h"
#include <fstream>
#include <sstream>
#include <filesystem>
#include <set>

#include "HotReload.h"

std::thread g_fileWatchThread;

namespace GeckFuncs
{
	auto CompileScript = reinterpret_cast<void(__thiscall*)(void*, Script*, int)>(0x5C9800);
}

void* g_scriptContext = reinterpret_cast<void*>(0xECFDF8);

extern std::atomic<bool> g_updateFromFile;

void FileWatchThread(int dummy)
{
	try
	{
		auto* handle = FindFirstChangeNotification(GetScriptsDir().c_str(), true, FILE_NOTIFY_CHANGE_LAST_WRITE);
		if (handle == INVALID_HANDLE_VALUE || !handle)
			throw std::exception(FormatString("Could not find directory %s", GetScriptsDir().c_str()).c_str());
		while (true)
		{
			auto waitStatus = WaitForSingleObject(handle, 20);
			if (waitStatus == WAIT_TIMEOUT)
			{
				g_updateFromFile = false;
				waitStatus = WaitForSingleObject(handle, INFINITE);
			}
			if (waitStatus == WAIT_FAILED)
				throw std::exception("Failed to wait");
			if (!g_updateFromFile)
			{
				std::map<Script*, std::filesystem::path> queuedScripts;
				for (std::filesystem::recursive_directory_iterator next(GetScriptsDir()), end; next != end; ++next)
				{
					auto fileName = next->path().filename().string();
					std::string scriptName;
					if (ends_with(fileName, ".gek"))
						scriptName = fileName.substr(0, fileName.size() - 4);
					else if (ends_with(fileName, ".geck"))
						scriptName = fileName.substr(0, fileName.size() - 5);
					if (!scriptName.empty())
					{
						auto* form = GetFormByID(scriptName.c_str());
						if (form)
						{
							auto* script = DYNAMIC_CAST(form, TESForm, Script);
							if (script)
							{
								auto result = queuedScripts.emplace(script, next->path());

								static std::set<Script*> s_warned;
								if (!result.second && s_warned.find(script) == s_warned.end())
								{
									Log(FormatString("Warning: script %s has multiple files in Scripts folder, there MUST only be one!", script->editorData.editorID.CStr()), true);
									s_warned.emplace(script);
								}
							}
						}
					}
				}

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
						if (_stricmp(str.c_str(), script->text) != 0)
						{
							FormHeap_Free(script->text);
							script->text = static_cast<char*>(FormHeap_Allocate(str.size() + 1));
							strcpy_s(script->text, str.size() + 1, str.c_str());
							GeckFuncs::CompileScript(g_scriptContext, script, 0);
							SendHotReloadDataHook(script); // TODO: use plugin message when nvse 6.07 comes out
							Log(FormatString("Compiled script '%s' from path '%s'", script->editorData.editorID.CStr(), path.string().c_str()));
						}
					}
				}
			}
			if (!FindNextChangeNotification(handle))
				throw std::exception("Failed to read again handle.");
		}
	}
	catch (std::exception& e)
	{
		Log(FormatString("Compile from file error: %s (%s)", e.what(), GetLastErrorString().c_str()));
	}
	catch (...)
	{
		Log("Error in CompileScriptFromFile.cpp, please open a bug report on how this happened", true);
	}
}




void InitializeCompileFromFile()
{
	g_fileWatchThread = std::thread(FileWatchThread, 0);
	g_fileWatchThread.detach();
	if (!std::filesystem::exists(GetScriptsDir()))
		std::filesystem::create_directory(GetScriptsDir());
	Log("Scripts can now be edited directly from file in folder " + GetScriptsDir());
}