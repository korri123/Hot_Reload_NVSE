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

namespace GeckFuncs
{
	auto CompileScript = reinterpret_cast<void(__thiscall*)(void*, Script*, int)>(0x5C9800);
}

void* g_scriptContext = reinterpret_cast<void*>(0xECFDF8);

void FileWatchThread(int dummy)
{
	try
	{
		auto* handle = FindFirstChangeNotification(GetScriptsDir().c_str(), true, FILE_NOTIFY_CHANGE_LAST_WRITE);
		while (true)
		{
			if (handle == INVALID_HANDLE_VALUE || !handle)
				throw std::exception(FormatString("Could not find directory %s", GetScriptsDir().c_str()).c_str());
			const auto waitStatus = WaitForSingleObject(handle, INFINITE);
			if (waitStatus == WAIT_FAILED)
				throw std::exception("Failed to wait");
			for (std::filesystem::recursive_directory_iterator next(std::filesystem::path(GetScriptsDir().c_str())), end; next != end; ++next)
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
							std::ifstream t(next->path());
							std::stringstream buffer;
							buffer << t.rdbuf();
							auto str = buffer.str();
							str = ReplaceAll(str, "\r\n", "\n"); // lol
							str = ReplaceAll(str, "\n", "\r\n");
							if (_stricmp(str.c_str(), script->text) != 0)
							{
								FormHeap_Free(script->text);
								script->text = static_cast<char*>(FormHeap_Allocate(str.size() + 1));
								strcpy_s(script->text, str.size() + 1, str.c_str());
								GeckFuncs::CompileScript(g_scriptContext, script, 0);
							}
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
		GeckExtenderMessageLog("Compile from file error: %s (%s)", e.what(), GetLastErrorString().c_str());
	}
}

std::thread g_fileWatchThread;


void InitializeCompileFromFile()
{
	g_fileWatchThread = std::thread(FileWatchThread, 0);
	g_fileWatchThread.detach();
}