#include "MeleeNET.h"
#include <wx/msgdlg.h>
#include "Common/TraversalClient.h"
#include "DolphinWX/NetPlay/NetPlayLauncher.h"
#include "DolphinWX/GameListCtrl.h"
#include "Common/FileUtil.h"

wxString MeleeNET::m_netplay_code = wxString("");
bool MeleeNET::m_netplay = false;
bool MeleeNET::m_netplay_host = false;
std::string MeleeNET::m_netplay_client_string = "SmashLadder Dolphin";
bool MeleeNET::initalized = true;
std::string MeleeNET::expectedPlayerNames[3] = {"","",""};
int MeleeNET::currentPlayerCount = 0;
bool MeleeNET::spec = false;
long MeleeNET::expectedPlayerCount = 2;

void MeleeNET::onLogin() {
	std::string g_OldServer = "stun.dolphin - emu.org";
	u16 g_OldServerPort = 6262;
	u16 g_OldListenPort = 0;

	ENetAddress addr = { ENET_HOST_ANY, g_OldListenPort };
	ENetHost* host = enet_host_create(&addr,  // address
		50,     // peerCount
		1,      // channelLimit
		0,      // incomingBandwidth
		0);     // outgoingBandwidth
	if (!host)
	{
		g_MainNetHost.reset();
	}
	g_MainNetHost.reset(host);
	g_TraversalClient.reset(new TraversalClient(g_MainNetHost.get(), g_OldServer, g_OldServerPort));
	std::string netplay_code = std::string(wxString(g_TraversalClient->m_HostId.data(), g_TraversalClient->m_HostId.size()).mb_str());
}

void MeleeNET::playerJoinServer(std::string playername, NetPlayServer *server) {
	std::string *expectedNames = std::find(std::begin(MeleeNET::expectedPlayerNames), std::end(MeleeNET::expectedPlayerNames), playername);
	if (expectedNames == std::end(MeleeNET::expectedPlayerNames)) {
		//if name is found
		currentPlayerCount++;
		if (expectedPlayerCount == currentPlayerCount) {
			//Start netplay
			server->StartGame();
		}
	}
	else {
		//otherwise...
		
	}
}