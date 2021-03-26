#include <filesystem>

#include "GameScript.h"
#include <thread>
#include "GameData.h"
#include <fstream>
#include <sstream>
#include "CreateScriptFiles.h"

#include "SafeWrite.h"

std::thread g_createFilesThread;

extern std::string g_createFileExtension;

std::atomic<bool> g_updateFromFile = false;

void TryAnotherFilePath(std::string& filePath, const char* scriptName)
{
	for (std::filesystem::recursive_directory_iterator iter(GetScriptsDir()), end; iter != end; ++iter)
	{
		if (_stricmp(iter->path().stem().string().c_str(), scriptName) == 0)
		{
			filePath = iter->path().string();
			break;
		}
	}
}

extern bool g_openScriptsFolder;


void CreateFilesThread(bool overwrite)
{
	try
	{
		g_updateFromFile = true;
		auto* activeMod = DataHandler::Get()->activeFile;
		if (!activeMod)
			throw std::exception("Failed to get active mod");
		std::vector<Script*> scripts;
		for (auto iter = DataHandler::Get()->scriptList.Begin(); !iter.End(); ++iter)
		{
			if (*iter && iter->mods.Contains([&](ModInfo& item) { return activeMod == &item; }) && ValidString(iter->text) && ValidString(iter->editorData.editorID.CStr()))
			{
				scripts.push_back(*iter);
			}
		}
		const auto path = GetScriptsDir() + '\\' + std::string(activeMod->name);
		if (!scripts.empty())
		{
			bool created = false;
			if (!std::filesystem::exists(path))
			{
				std::filesystem::create_directory(path);
				created = true;
			}
			for (auto* script : scripts)
			{
				auto filePath = FormatString("%s\\%s.%s", path.c_str(), script->editorData.editorID.CStr(), g_createFileExtension.c_str());
				//if (!std::filesystem::exists(filePath))
				//	TryAnotherFilePath(filePath, script->editorData.editorID.CStr());
				std::ifstream ifs(filePath);
				std::stringstream ss;
				ss << ifs.rdbuf();
				auto fileText = ss.str();
				fileText = ReplaceAll(fileText, "\r\n", "\n");
				fileText = ReplaceAll(fileText, "\n", "\r\n");
				ifs.close();
				if (_stricmp(fileText.c_str(), script->text) != 0)
				{
					if (std::filesystem::exists(filePath) && !overwrite)
						continue;
					std::ofstream ofs(filePath, std::ios_base::out | std::ios_base::trunc);
					ofs << ReplaceAll(script->text, "\r\n", "\n");
					ofs.close();
				}
			}
			if (created)
			{
				Log(FormatString("Created %d script files in %s", scripts.size(), path.c_str()));
			}
			if (g_openScriptsFolder && !overwrite && std::filesystem::exists(path))
			{
				ShellExecute(nullptr, "open", path.c_str(), nullptr, nullptr, SW_SHOWDEFAULT);
			}
		}
	}
	catch (std::exception& e)
	{
		Log(FormatString("Error: %s", e.what()), false);
	}
	catch (...)
	{
		Log("Error in CreateScriptFiles.cpp, please open a bug report on how this happened", true);
	}
	
}

extern bool g_enableCreateFiles;

void CreateScriptFiles()
{
	if (g_enableCreateFiles)
	{
		g_createFilesThread = std::thread(CreateFilesThread, true);
		g_createFilesThread.detach();
	}
}

void __stdcall LoadPluginCreateScriptFiles()
{
	if (g_enableCreateFiles)
	{
		g_createFilesThread = std::thread(CreateFilesThread, false);
		g_createFilesThread.detach();
	}
}

__declspec(naked) void HookPostPluginLoad()
{
	static const auto hookedCall = 0x464A90;
	static const auto returnAddress = 0x445205;
	__asm
	{
		call hookedCall
		add esp, 8
		call LoadPluginCreateScriptFiles
		jmp returnAddress
	}
}

void PatchPostPluginLoad()
{
	WriteRelJump(0x4451FD, UInt32(HookPostPluginLoad));
}


