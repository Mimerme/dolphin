#pragma once
#include <wx/string.h>
#include <string>

class MeleeNET {
public:
	static wxString m_netplay_code;
	static bool m_netplay;
	static bool m_netplay_host;
	//Allows different revisions of Dolphin to connect
	static std::string m_netplay_client_string;
	static bool initalized;
	static void onLogin();
};
