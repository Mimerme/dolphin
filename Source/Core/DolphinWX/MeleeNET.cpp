#include "MeleeNET.h"
#include <wx/msgdlg.h>
#include "Common/TraversalClient.h"
#include "DolphinWX/NetPlay/NetPlayLauncher.h"
#include "DolphinWX/GameListCtrl.h"
#include "Common/FileUtil.h"
#include "Core/ConfigManager.h"

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
			//Set some netplaya configurations, this NEEDS to be here
			SConfig& instance = SConfig::GetInstance();
			NetSettings settings;
			settings.m_CPUthread = instance.bCPUThread;
			settings.m_CPUcore = instance.iCPUCore;
			settings.m_EnableCheats = instance.bEnableCheats;
			settings.m_SelectedLanguage = instance.SelectedLanguage;
			settings.m_OverrideGCLanguage = instance.bOverrideGCLanguage;
			settings.m_ProgressiveScan = instance.bProgressive;
			settings.m_PAL60 = instance.bPAL60;
			settings.m_DSPHLE = instance.bDSPHLE;
			settings.m_DSPEnableJIT = instance.m_DSPEnableJIT;
			settings.m_WriteToMemcard = false;
			settings.m_OCEnable = instance.m_OCEnable;
			settings.m_OCFactor = instance.m_OCFactor;
			settings.m_EXIDevice[0] = instance.m_EXIDevice[0];
			settings.m_EXIDevice[1] = instance.m_EXIDevice[1];

			//Start netplay
			server->SetNetSettings(settings);
			server->StartGame();
		}
	}
	else {
		//otherwise...
		
	}
}
