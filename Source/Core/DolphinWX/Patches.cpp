//IMPORTANT: These patches are to applied through the C++ preprocessor
//This is to ease the development of future versions and prevent merge conflicts with FasterMelee and Dolphin

//#Usage : Main.cpp
//Adds 'netplay' argument to dolphin
#define ADD_ARGUMENT { wxCMD_LINE_OPTION, "n", "netplay", "Starts/Joins a netplay server", wxCMD_LINE_VAL_STRING,\
	wxCMD_LINE_PARAM_OPTIONAL }, { wxCMD_LINE_SWITCH, "s", "spectator", "Enable or disable spectators for the lobby", wxCMD_LINE_VAL_NONE,\
wxCMD_LINE_PARAM_OPTIONAL }, { wxCMD_LINE_OPTION, "players", "playercount", "Expected number of players before the game starts (Host Only)", wxCMD_LINE_VAL_NUMBER,\
wxCMD_LINE_PARAM_OPTIONAL },\
{ wxCMD_LINE_OPTION, "player1","player1", "Player 2 expected name (Host Only)", wxCMD_LINE_VAL_STRING,\
wxCMD_LINE_PARAM_OPTIONAL },\
{ wxCMD_LINE_OPTION, "player2", "player2", "Player 3 expected name (Host Only)", wxCMD_LINE_VAL_STRING,\
wxCMD_LINE_PARAM_OPTIONAL },\
{ wxCMD_LINE_OPTION, "player3", "player3", "Player 4 expected name (Host Only)", wxCMD_LINE_VAL_STRING,\
wxCMD_LINE_PARAM_OPTIONAL },

//#Usage : Main.cpp
//Auto connect to netplay
#define ADD_PARSER MeleeNET::m_netplay = parser.Found("netplay", &MeleeNET::m_netplay_code); if (MeleeNET::m_netplay_code.Contains("host")) {MeleeNET::m_netplay_host = true;} \
  MeleeNET::spec = parser.Found("spectator");\
wxString temp;\
parser.Found("playercount", &MeleeNET::expectedPlayerCount);\
if (parser.Found("player1", &temp)) {\
	MeleeNET::expectedPlayerNames[0] = std::string(temp.mb_str());\
}\
if (parser.Found("player2", &temp)) {\
	MeleeNET::expectedPlayerNames[1] = std::string(temp.mb_str());\
}\
if (parser.Found("player3", &temp)) {\
	MeleeNET::expectedPlayerNames[1] = std::string(temp.mb_str());\
}



//#Usage FrameTools.cpp
//Makes OnNetplay Accessable publicly
#define ONNETPLAY_PUBLIC void OnNetPlay(wxCommandEvent& event);

#define ONJOIN_PUBLIC void OnJoin(wxCommandEvent& event);

#define ONHOST_PUBLIC void OnHost(wxCommandEvent& event);


//#Usage Main.cpp
//Called after main window initalization
#define AFTER_INIT if(MeleeNET::m_netplay && !MeleeNET::m_netplay_host) {\
wxGetApp().GetCFrame()->OnNetPlay(wxCommandEvent());wxGetApp().GetCFrame()->g_NetPlaySetupDiag->OnJoin(wxCommandEvent());}\
if (MeleeNET::m_netplay && MeleeNET::m_netplay_host) {\
wxGetApp().GetCFrame()->OnNetPlay(wxCommandEvent());\
wxGetApp().GetCFrame()->g_NetPlaySetupDiag->OnHost(wxCommandEvent());}

#define IF_NETPLAY if (MeleeNET::m_netplay) {\
join_config.use_traversal = true;\
join_config.player_name = "PlaceholderName";\
join_config.game_list_ctrl = m_game_list;\
join_config.SetDialogInfo(netplay_section, m_parent);\
}

#define IF_NETPLAY_SET_CODE if(MeleeNET::m_netplay){join_config.connect_hash_code = std::string(MeleeNET::m_netplay_code.mb_str());}

#define INITALIZE_MELEENET \
wxString MeleeNET::m_netplay_code = wxString("");\
bool MeleeNET::m_netplay = false;\
bool MeleeNET::m_netplay_host = false;

#define GAME_WINDOW_TITLE std::string titleStr = StringFromFormat("%s | %s", "SmashLadder Custom Dolphin", str.c_str());

#define ADD_TOOLBAR_ITEM  AddToolBarButton(IDM_CONFIG_CONTROLLERS, TOOLBAR_CONTROLLER, _("Connect Account"), _("Connect your Dolphin to Anther's Smashladder"));
#define ADD_TOOLBAR_ITEM_BITMAP  TOOLBAR_LOGIN,
#define BIND_TOOLBAR_ITEM Bind(wxEVT_MENU, &CFrame::OnLogin, this, IDM_CONFIG_CONTROLLERS);
#define TOOLBAR_ITEM_FUNCTION void CFrame::OnLogin(wxCommandEvent& WXUNUSED(event)) {MeleeNET::onLogin();}

#define SETUP_LOGGER wxLog* logger = new wxLogStream(&std::cout);\
wxLog::SetActiveTarget(logger);\

#define MELEENET_LOG(message) wxLogMessage(message);

#define NETPLAY_SET_VERSION spac << MeleeNET::m_netplay_client_string;

#define HOST_SET_GAME host_config.game_name = "SUPER SMASH BROS. Melee (GALE01, Revision 2)";\
host_config.use_traversal = true;\
host_config.player_name = "HostTest";\
host_config.game_list_ctrl = m_game_list;\
host_config.SetDialogInfo(netplay_section, m_parent);\

#define HOST_STRING std::string netplay_code(std::begin(m_HostId), std::end(m_HostId));\
std::string netplay_code_label = "Host Code ";\
std::string netplay_output = netplay_code_label.append(netplay_code);

/*
//#Usage NetPlayClient.h
//Def of NETPLAY_CUSTOM in the header file
#define DEFINE_NETPLAY_CUSTOM NetPlayClient(const std::string & address, const u16 port, const std::string & name, bool traversal, const std::string & centralServer, u16 centralPort);

//#Usage NetPlayClient.cpp
//Modified function to run netplay without the need of a UI
#define NETPLAY_CUSTOM NetPlayClient::NetPlayClient(const std::string& address, const u16 port, const std::string& name, bool traversal, const std::string& centralServer, u16 centralPort) : m_player_name(name) { ClearBuffers(); if(traversal) { if (address.size() > NETPLAY_CODE_SIZE) { PanicAlertT("Host code size is to large.\nPlease recheck that you have the correct code"); return; } if (!EnsureTraversalClient(centralServer, centralPort)) return; m_client = g_MainNetHost.get(); m_traversal_client = g_TraversalClient.get(); if (m_traversal_client->m_State == TraversalClient::Failure) m_traversal_client->ReconnectToServer(); m_traversal_client->m_Client = this; m_host_spec = address; m_connection_state = ConnectionState::WaitingForTraversalClientConnection; OnTraversalStateChanged(); m_connecting = true; Common::Timer connect_timer; connect_timer.Start(); while (m_connecting) { ENetEvent netEvent; if (m_traversal_client) m_traversal_client->HandleResends(); while (enet_host_service(m_client, &netEvent, 4) > 0) { sf::Packet rpac; switch (netEvent.type) { case ENET_EVENT_TYPE_CONNECT: m_server = netEvent.peer; if (Connect()) { m_connection_state = ConnectionState::Connected; m_thread = std::thread(&NetPlayClient::ThreadFunc, this); } return; default: break; } } if (connect_timer.GetTimeElapsed() > 5000) break; } PanicAlertT("Failed To Connect!"); } }

//#Usage NetplayClient.h
//Indicates a code block to skip if no UI has been initalized
#define NETPLAY_NOUI if(!m_netplay){
#define NETPLAY_NOUI_END }
*/