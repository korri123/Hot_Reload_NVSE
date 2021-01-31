#include "ScriptCommands.h"
#include "GameScript.h"
#include "GameAPI.h"
#include "SocketUtils.h"
#include "OpenInGeck.h"
#include "GameObjects.h"

UnorderedSet<UInt32> g_gameHotLoadedScripts;


bool Cmd_GetGameHotReloaded_Execute(COMMAND_ARGS)
{
	if (scriptObj && g_gameHotLoadedScripts.Erase(scriptObj->refID))
		*result = 1;
	else *result = 0;

	return true;
}


bool Cmd_ToGeck_Execute(COMMAND_ARGS)
{
	TESForm* form = nullptr;
	bool success = false;
	if (ExtractArgs(EXTRACT_ARGS, &form))
		success = true;
	if (!thisObj && !form || !success)
	{
		Console_Print("Reference or base form missing");
		return true;
	}

	if (!form)
		form = thisObj;

	if (form->GetModIndex() == 0xFF)
	{
		Console_Print("ToGECK: ref %X is dynamically placed; sending base form instead...", form->refID);
		form = form->TryGetREFRParent();
	}
	
	try
	{
		SocketClient client("127.0.0.1", g_geckPort);
		client.SendData(GeckTransferObject(TransferType::kOpenRef));
		client.SendData(GeckOpenRefTransferObject(form->refID));
	}
	catch (const SocketException& e)
	{
		Console_Print("Failed to communicate with the GECK!");
		Console_Print(e.what());
	}
	return true;
}