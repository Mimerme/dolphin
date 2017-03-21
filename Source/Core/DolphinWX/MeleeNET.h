#pragma once
#include <wx/string.h>
#include <string>
#include "Core\NetPlayServer.h"

class MeleeNET {
public:
	static wxString m_netplay_code;
	static bool m_netplay;
	static bool m_netplay_host;

	static wxString playername;
	static long expectedPlayerCount;
	static bool spec;
	static std::string expectedPlayerNames[3];
	static int currentPlayerCount;
	//Allows different revisions of Dolphin to connect
	static std::string m_netplay_client_string;
	static bool initalized;
	static void onLogin();
	static void playerJoinServer(std::string, NetPlayServer *server);
};
