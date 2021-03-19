#include <functional>
#include <queue>

#include "OpenInGeck.h"
#include <thread>
#include "SocketUtils.h"
#include "GameAPI.h"
#include "HotReload.h"


std::queue<std::function<void()>> g_editorMainWindowExecutionQueue;

void MainWindowCallback()
{
	while (!g_editorMainWindowExecutionQueue.empty())
	{
		const auto& top = g_editorMainWindowExecutionQueue.back();
		top();
		g_editorMainWindowExecutionQueue.pop();
	}
}

void HandleOpenRef(SocketServer& server)
{
	GeckOpenRefTransferObject obj;
	server.ReadData(obj);
	auto* window = GetGeckWindow();
	if (!window)
		return;
	g_editorMainWindowExecutionQueue.push([=]()
	{
		auto* form = LookupFormByID(obj.refId);
		if (!form)
		{
			ShowErrorMessageBox(FormatString("Tried to load unloaded or invalid form id %X", obj.refId).c_str());
			return;
		}
		(*reinterpret_cast<void(__thiscall**)(__int32, HWND, __int32, __int32)>(*reinterpret_cast<__int32*>(form) + 0x164))(
			reinterpret_cast<UInt32>(form), window, 0, 1);
		SetForegroundWindow(window);
		GeckExtenderMessageLog("Opened reference %X", obj.refId);
	});
	SendMessage(window, WM_COMMAND, 0xFEED, reinterpret_cast<LPARAM>(MainWindowCallback));
}

void HandleOpenInGeck()
{
	static SocketServer s_server(g_geckPort);
	s_server.WaitForConnection();
	GeckTransferObject geckTransferObject;
	s_server.ReadData(geckTransferObject);
	switch (geckTransferObject.type)
	{
	case TransferType::kOpenRef:
		{
			HandleOpenRef(s_server);
			break;
		}
	default:
		{
			ShowErrorMessageBox("Invalid GECK transfer!");
			break;
		}
	}
}

void GeckThread(int _)
{
	try
	{
		while (true)
		{
			HandleOpenInGeck();
		}
	}
	catch (const SocketException& e)
	{
		Log("ToGECK server failed! Check hot_reload.log for info", true);
		Log(FormatString("Error: %s", e.what()));
	}
	catch (...)
	{
		Log("Critical error in OpenInGeckEditor.cpp, please open a bug report on how this happened", true);
	}
}

void StartGeckServer()
{
	auto thread = std::thread(GeckThread, 0);
	thread.detach();
}
