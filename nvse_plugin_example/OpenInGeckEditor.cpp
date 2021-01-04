#include "OpenInGeck.h"
#include <thread>
#include "SocketUtils.h"
#include "GameAPI.h"


HWND__* g_hwnd;

void HandleOpenRef(SocketServer& server)
{
	GeckOpenRefTransferObject obj;
	server.ReadData(obj);
	auto* form = LookupFormByID(obj.refId);
	if (!form)
	{
		ShowErrorMessageBox(FormatString("Tried to load unloaded or invalid form id %X", obj.refId).c_str());
		return;
	}
	if (!g_hwnd)
	{
		auto* window = FindWindow("Garden of Eden Creation Kit", nullptr);
		if (!window)
		{
			ShowErrorMessageBox("Failed to find window!");
			return;
		}
		g_hwnd = window;
	}
	(*reinterpret_cast<void(__thiscall**)(__int32, HWND, __int32, __int32)>(*reinterpret_cast<__int32*>(form) + 0x164))(
		reinterpret_cast<UInt32>(form), g_hwnd, 0, 1);
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
		ShowErrorMessageBox("GECK hot reload server failed! Check hot_reload.log for info");
		_MESSAGE("Error: %s", e.what());
	}
}

void StartGeckServer()
{
	auto thread = std::thread(GeckThread, 0);
	thread.detach();
}
