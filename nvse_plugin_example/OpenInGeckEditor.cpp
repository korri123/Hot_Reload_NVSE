/*
#include <thread>
#include "SocketUtils.h"
#include "GameAPI.h"

const UInt16 g_geckPort = 12058;

void HandleOpenInGECK()
{
	static SocketServer s_server(g_geckPort);
}

void GECKThread(int _)
{
	try
	{
		while (true)
		{
			HandleOpenInGECK();
		}
	}
	catch (const SocketException& e)
	{
		//ShowCompilerError()
	}
}

void StartGECKServer()
{
	auto thread = std::thread(GECKThread, 0);
	thread.detach();
}*/