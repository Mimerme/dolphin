// Copyright 2008 Dolphin Emulator Project
// Licensed under GPLv2+
// Refer to the license.txt file included.

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#ifndef _WIN32
#include <netdb.h>
#include <poll.h>
#endif

#include "Common/Assert.h"
#include "Common/CommonFuncs.h"
#include "Common/CommonPaths.h"
#include "Common/FileUtil.h"
#include "Common/Logging/Log.h"
#include "Common/NandPaths.h"
#include "Common/Network.h"
#include "Common/SettingsHandler.h"
#include "Common/StringUtil.h"
#include "Core/ConfigManager.h"
#include "Core/Core.h"
#include "Core/IOS/Network/ICMP.h"
#include "Core/IOS/Network/Net.h"
#include "Core/IOS/Network/Socket.h"
#include "Core/ec_wii.h"

#ifdef _WIN32
#include <iphlpapi.h>
#include <ws2tcpip.h>

#define MALLOC(x) HeapAlloc(GetProcessHeap(), 0, (x))
#define FREE(x) HeapFree(GetProcessHeap(), 0, (x))

#elif defined(__linux__) or defined(__APPLE__)
#include <netinet/in.h>
#include <sys/socket.h>

typedef struct pollfd pollfd_t;
#else
#include <errno.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/types.h>
#endif

// WSAPoll doesn't support POLLPRI and POLLWRBAND flags
#ifdef _WIN32
#define UNSUPPORTED_WSAPOLL POLLPRI | POLLWRBAND
#else
#define UNSUPPORTED_WSAPOLL 0
#endif

namespace IOS
{
namespace HLE
{
namespace Device
{
// **********************************************************************************
// Handle /dev/net/kd/request requests
NetKDRequest::NetKDRequest(u32 device_id, const std::string& device_name)
    : Device(device_id, device_name)
{
}

NetKDRequest::~NetKDRequest()
{
  WiiSockMan::GetInstance().Clean();
}

IPCCommandResult NetKDRequest::IOCtl(const IOCtlRequest& request)
{
  s32 return_value = 0;
  switch (request.request)
  {
  case IOCTL_NWC24_SUSPEND_SCHEDULAR:
    // NWC24iResumeForCloseLib  from NWC24SuspendScheduler (Input: none, Output: 32 bytes)
    INFO_LOG(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_SUSPEND_SCHEDULAR - NI");
    Memory::Write_U32(0, request.buffer_out);  // no error
    break;

  case IOCTL_NWC24_EXEC_TRY_SUSPEND_SCHEDULAR:  // NWC24iResumeForCloseLib
    INFO_LOG(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_EXEC_TRY_SUSPEND_SCHEDULAR - NI");
    break;

  case IOCTL_NWC24_EXEC_RESUME_SCHEDULAR:  // NWC24iResumeForCloseLib
    INFO_LOG(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_EXEC_RESUME_SCHEDULAR - NI");
    Memory::Write_U32(0, request.buffer_out);  // no error
    break;

  case IOCTL_NWC24_STARTUP_SOCKET:  // NWC24iStartupSocket
    Memory::Write_U32(0, request.buffer_out);
    Memory::Write_U32(0, request.buffer_out + 4);
    return_value = 0;
    INFO_LOG(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_STARTUP_SOCKET - NI");
    break;

  case IOCTL_NWC24_CLEANUP_SOCKET:
    INFO_LOG(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_CLEANUP_SOCKET - NI");
    break;

  case IOCTL_NWC24_LOCK_SOCKET:  // WiiMenu
    INFO_LOG(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_LOCK_SOCKET - NI");
    break;

  case IOCTL_NWC24_UNLOCK_SOCKET:
    INFO_LOG(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_UNLOCK_SOCKET - NI");
    break;

  case IOCTL_NWC24_REQUEST_REGISTER_USER_ID:
    INFO_LOG(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_REQUEST_REGISTER_USER_ID");
    Memory::Write_U32(0, request.buffer_out);
    Memory::Write_U32(0, request.buffer_out + 4);
    break;

  case IOCTL_NWC24_REQUEST_GENERATED_USER_ID:  // (Input: none, Output: 32 bytes)
    INFO_LOG(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_REQUEST_GENERATED_USER_ID");
    if (config.CreationStage() == NWC24::NWC24Config::NWC24_IDCS_INITIAL)
    {
      std::string settings_Filename(
          Common::GetTitleDataPath(TITLEID_SYSMENU, Common::FROM_SESSION_ROOT) + WII_SETTING);
      SettingsHandler gen;
      std::string area, model;
      bool _GotSettings = false;

      if (File::Exists(settings_Filename))
      {
        File::IOFile settingsFileHandle(settings_Filename, "rb");
        if (settingsFileHandle.ReadBytes((void*)gen.GetData(), SettingsHandler::SETTINGS_SIZE))
        {
          gen.Decrypt();
          area = gen.GetValue("AREA");
          model = gen.GetValue("MODEL");
          _GotSettings = true;
        }
      }
      if (_GotSettings)
      {
        u8 area_code = GetAreaCode(area);
        u8 id_ctr = config.IdGen();
        u8 hardware_model = GetHardwareModel(model);

        const EcWii& ec = EcWii::GetInstance();
        u32 HollywoodID = ec.GetNGID();
        u64 UserID = 0;

        s32 ret = NWC24MakeUserID(&UserID, HollywoodID, id_ctr, hardware_model, area_code);
        config.SetId(UserID);
        config.IncrementIdGen();
        config.SetCreationStage(NWC24::NWC24Config::NWC24_IDCS_GENERATED);
        config.WriteConfig();

        Memory::Write_U32(ret, request.buffer_out);
      }
      else
      {
        Memory::Write_U32(NWC24::WC24_ERR_FATAL, request.buffer_out);
      }
    }
    else if (config.CreationStage() == NWC24::NWC24Config::NWC24_IDCS_GENERATED)
    {
      Memory::Write_U32(NWC24::WC24_ERR_ID_GENERATED, request.buffer_out);
    }
    else if (config.CreationStage() == NWC24::NWC24Config::NWC24_IDCS_REGISTERED)
    {
      Memory::Write_U32(NWC24::WC24_ERR_ID_REGISTERED, request.buffer_out);
    }
    Memory::Write_U64(config.Id(), request.buffer_out + 4);
    Memory::Write_U32(config.CreationStage(), request.buffer_out + 0xC);
    break;

  case IOCTL_NWC24_GET_SCHEDULAR_STAT:
    INFO_LOG(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_GET_SCHEDULAR_STAT - NI");
    break;

  case IOCTL_NWC24_SAVE_MAIL_NOW:
    INFO_LOG(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_SAVE_MAIL_NOW - NI");
    break;

  case IOCTL_NWC24_REQUEST_SHUTDOWN:
    // if ya set the IOS version to a very high value this happens ...
    INFO_LOG(IOS_WC24, "NET_KD_REQ: IOCTL_NWC24_REQUEST_SHUTDOWN - NI");
    break;

  default:
    request.Log(GetDeviceName(), LogTypes::IOS_WC24);
  }

  return GetDefaultReply(return_value);
}

u8 NetKDRequest::GetAreaCode(const std::string& area) const
{
  static std::map<const std::string, u8> regions = {
      {"JPN", 0}, {"USA", 1}, {"EUR", 2}, {"AUS", 2}, {"BRA", 1}, {"TWN", 3}, {"ROC", 3},
      {"KOR", 4}, {"HKG", 5}, {"ASI", 5}, {"LTN", 1}, {"SAF", 2}, {"CHN", 6},
  };

  auto entryPos = regions.find(area);
  if (entryPos != regions.end())
    return entryPos->second;
  else
    return 7;  // Unknown
}

u8 NetKDRequest::GetHardwareModel(const std::string& model) const
{
  static std::map<const std::string, u8> models = {
      {"RVL", MODEL_RVL}, {"RVT", MODEL_RVT}, {"RVV", MODEL_RVV}, {"RVD", MODEL_RVD},
  };

  auto entryPos = models.find(model);
  if (entryPos != models.end())
    return entryPos->second;
  else
    return MODEL_ELSE;
}

static inline u8 u64_get_byte(u64 value, u8 shift)
{
  return (u8)(value >> (shift * 8));
}

static inline u64 u64_insert_byte(u64 value, u8 shift, u8 byte)
{
  u64 mask = 0x00000000000000FFULL << (shift * 8);
  u64 inst = (u64)byte << (shift * 8);
  return (value & ~mask) | inst;
}

s32 NetKDRequest::NWC24MakeUserID(u64* nwc24_id, u32 hollywood_id, u16 id_ctr, u8 hardware_model,
                                  u8 area_code)
{
  const u8 table2[8] = {0x1, 0x5, 0x0, 0x4, 0x2, 0x3, 0x6, 0x7};
  const u8 table1[16] = {0x4, 0xB, 0x7, 0x9, 0xF, 0x1, 0xD, 0x3,
                         0xC, 0x2, 0x6, 0xE, 0x8, 0x0, 0xA, 0x5};

  u64 mix_id = ((u64)area_code << 50) | ((u64)hardware_model << 47) | ((u64)hollywood_id << 15) |
               ((u64)id_ctr << 10);
  u64 mix_id_copy1 = mix_id;

  int ctr = 0;
  for (ctr = 0; ctr <= 42; ctr++)
  {
    u64 value = mix_id >> (52 - ctr);
    if (value & 1)
    {
      value = 0x0000000000000635ULL << (42 - ctr);
      mix_id ^= value;
    }
  }

  mix_id = (mix_id_copy1 | (mix_id & 0xFFFFFFFFUL)) ^ 0x0000B3B3B3B3B3B3ULL;
  mix_id = (mix_id >> 10) | ((mix_id & 0x3FF) << (11 + 32));

  for (ctr = 0; ctr <= 5; ctr++)
  {
    u8 ret = u64_get_byte(mix_id, ctr);
    u8 foobar = ((table1[(ret >> 4) & 0xF]) << 4) | (table1[ret & 0xF]);
    mix_id = u64_insert_byte(mix_id, ctr, foobar & 0xff);
  }
  u64 mix_id_copy2 = mix_id;

  for (ctr = 0; ctr <= 5; ctr++)
  {
    u8 ret = u64_get_byte(mix_id_copy2, ctr);
    mix_id = u64_insert_byte(mix_id, table2[ctr], ret);
  }

  mix_id &= 0x001FFFFFFFFFFFFFULL;
  mix_id = (mix_id << 1) | ((mix_id >> 52) & 1);

  mix_id ^= 0x00005E5E5E5E5E5EULL;
  mix_id &= 0x001FFFFFFFFFFFFFULL;

  *nwc24_id = mix_id;

  if (mix_id > 9999999999999999ULL)
    return NWC24::WC24_ERR_FATAL;

  return NWC24::WC24_OK;
}

static void SaveMacAddress(u8* mac)
{
  SConfig::GetInstance().m_WirelessMac = Common::MacAddressToString(mac);
  SConfig::GetInstance().SaveSettings();
}

static void GetMacAddress(u8* mac)
{
  // Parse MAC address from config, and generate a new one if it doesn't
  // exist or can't be parsed.
  std::string wireless_mac = SConfig::GetInstance().m_WirelessMac;

  if (Core::g_want_determinism)
    wireless_mac = "12:34:56:78:9a:bc";

  if (!Common::StringToMacAddress(wireless_mac, mac))
  {
    Common::GenerateMacAddress(Common::MACConsumer::IOS, mac);
    SaveMacAddress(mac);
    if (!wireless_mac.empty())
    {
      ERROR_LOG(IOS_NET, "The MAC provided (%s) is invalid. We have "
                         "generated another one for you.",
                Common::MacAddressToString(mac).c_str());
    }
  }
  INFO_LOG(IOS_NET, "Using MAC address: %s", Common::MacAddressToString(mac).c_str());
}

// **********************************************************************************
// Handle /dev/net/ncd/manage requests
NetNCDManage::NetNCDManage(u32 device_id, const std::string& device_name)
    : Device(device_id, device_name)
{
}

IPCCommandResult NetNCDManage::IOCtlV(const IOCtlVRequest& request)
{
  s32 return_value = IPC_SUCCESS;
  u32 common_result = 0;
  u32 common_vector = 0;

  switch (request.request)
  {
  case IOCTLV_NCD_LOCKWIRELESSDRIVER:
    break;

  case IOCTLV_NCD_UNLOCKWIRELESSDRIVER:
    // Memory::Read_U32(request.in_vectors.at(0).address);
    break;

  case IOCTLV_NCD_GETCONFIG:
    INFO_LOG(IOS_NET, "NET_NCD_MANAGE: IOCTLV_NCD_GETCONFIG");
    config.WriteToMem(request.io_vectors.at(0).address);
    common_vector = 1;
    break;

  case IOCTLV_NCD_SETCONFIG:
    INFO_LOG(IOS_NET, "NET_NCD_MANAGE: IOCTLV_NCD_SETCONFIG");
    config.ReadFromMem(request.in_vectors.at(0).address);
    break;

  case IOCTLV_NCD_READCONFIG:
    INFO_LOG(IOS_NET, "NET_NCD_MANAGE: IOCTLV_NCD_READCONFIG");
    config.ReadConfig();
    config.WriteToMem(request.io_vectors.at(0).address);
    common_vector = 1;
    break;

  case IOCTLV_NCD_WRITECONFIG:
    INFO_LOG(IOS_NET, "NET_NCD_MANAGE: IOCTLV_NCD_WRITECONFIG");
    config.ReadFromMem(request.in_vectors.at(0).address);
    config.WriteConfig();
    break;

  case IOCTLV_NCD_GETLINKSTATUS:
    INFO_LOG(IOS_NET, "NET_NCD_MANAGE: IOCTLV_NCD_GETLINKSTATUS");
    // Always connected
    Memory::Write_U32(Net::ConnectionSettings::LINK_WIRED, request.io_vectors.at(0).address + 4);
    break;

  case IOCTLV_NCD_GETWIRELESSMACADDRESS:
    INFO_LOG(IOS_NET, "NET_NCD_MANAGE: IOCTLV_NCD_GETWIRELESSMACADDRESS");

    u8 address[Common::MAC_ADDRESS_SIZE];
    GetMacAddress(address);
    Memory::CopyToEmu(request.io_vectors.at(1).address, address, sizeof(address));
    break;

  default:
    INFO_LOG(IOS_NET, "NET_NCD_MANAGE IOCtlV: %#x", request.request);
    break;
  }

  Memory::Write_U32(common_result, request.io_vectors.at(common_vector).address);
  if (common_vector == 1)
  {
    Memory::Write_U32(common_result, request.io_vectors.at(common_vector).address + 4);
  }
  return GetDefaultReply(return_value);
}

// **********************************************************************************
// Handle /dev/net/wd/command requests
NetWDCommand::NetWDCommand(u32 device_id, const std::string& device_name)
    : Device(device_id, device_name)
{
}

// This is just for debugging / playing around.
// There really is no reason to implement wd unless we can bend it such that
// we can talk to the DS.
IPCCommandResult NetWDCommand::IOCtlV(const IOCtlVRequest& request)
{
  s32 return_value = IPC_SUCCESS;

  switch (request.request)
  {
  case IOCTLV_WD_SCAN:
  {
    // Gives parameters detailing type of scan and what to match
    // XXX - unused
    // ScanInfo *scan = (ScanInfo *)Memory::GetPointer(request.in_vectors.at(0).m_Address);

    u16* results = (u16*)Memory::GetPointer(request.io_vectors.at(0).address);
    // first u16 indicates number of BSSInfo following
    results[0] = Common::swap16(1);

    BSSInfo* bss = (BSSInfo*)&results[1];
    memset(bss, 0, sizeof(BSSInfo));

    bss->length = Common::swap16(sizeof(BSSInfo));
    bss->rssi = Common::swap16(0xffff);

    for (int i = 0; i < BSSID_SIZE; ++i)
      bss->bssid[i] = i;

    const char* ssid = "dolphin-emu";
    strcpy((char*)bss->ssid, ssid);
    bss->ssid_length = Common::swap16((u16)strlen(ssid));

    bss->channel = Common::swap16(2);
  }
  break;

  case IOCTLV_WD_GET_INFO:
  {
    Info* info = (Info*)Memory::GetPointer(request.io_vectors.at(0).address);
    memset(info, 0, sizeof(Info));
    // Probably used to disallow certain channels?
    memcpy(info->country, "US", 2);
    info->ntr_allowed_channels = Common::swap16(0xfffe);

    u8 address[Common::MAC_ADDRESS_SIZE];
    GetMacAddress(address);
    memcpy(info->mac, address, sizeof(info->mac));
  }
  break;

  case IOCTLV_WD_GET_MODE:
  case IOCTLV_WD_SET_LINKSTATE:
  case IOCTLV_WD_GET_LINKSTATE:
  case IOCTLV_WD_SET_CONFIG:
  case IOCTLV_WD_GET_CONFIG:
  case IOCTLV_WD_CHANGE_BEACON:
  case IOCTLV_WD_DISASSOC:
  case IOCTLV_WD_MP_SEND_FRAME:
  case IOCTLV_WD_SEND_FRAME:
  case IOCTLV_WD_CALL_WL:
  case IOCTLV_WD_MEASURE_CHANNEL:
  case IOCTLV_WD_GET_LASTERROR:
  case IOCTLV_WD_CHANGE_GAMEINFO:
  case IOCTLV_WD_CHANGE_VTSF:
  case IOCTLV_WD_RECV_FRAME:
  case IOCTLV_WD_RECV_NOTIFICATION:
  default:
    request.Dump(GetDeviceName(), LogTypes::IOS_NET, LogTypes::LINFO);
  }

  return GetDefaultReply(return_value);
}

// **********************************************************************************
// Handle /dev/net/ip/top requests
NetIPTop::NetIPTop(u32 device_id, const std::string& device_name) : Device(device_id, device_name)
{
#ifdef _WIN32
  int ret = WSAStartup(MAKEWORD(2, 2), &InitData);
  INFO_LOG(IOS_NET, "WSAStartup: %d", ret);
#endif
}

NetIPTop::~NetIPTop()
{
#ifdef _WIN32
  WSACleanup();
#endif
}

static int inet_pton(const char* src, unsigned char* dst)
{
  int saw_digit, octets;
  char ch;
  unsigned char tmp[4], *tp;

  saw_digit = 0;
  octets = 0;
  *(tp = tmp) = 0;
  while ((ch = *src++) != '\0')
  {
    if (ch >= '0' && ch <= '9')
    {
      unsigned int newt = (*tp * 10) + (ch - '0');

      if (newt > 255)
        return 0;
      *tp = newt;
      if (!saw_digit)
      {
        if (++octets > 4)
          return 0;
        saw_digit = 1;
      }
    }
    else if (ch == '.' && saw_digit)
    {
      if (octets == 4)
        return 0;
      *++tp = 0;
      saw_digit = 0;
    }
    else
    {
      return 0;
    }
  }
  if (octets < 4)
    return 0;
  memcpy(dst, tmp, 4);
  return 1;
}

// Maps SOCKOPT level from native to Wii
static unsigned int opt_level_mapping[][2] = {{SOL_SOCKET, 0xFFFF}};

// Maps SOCKOPT optname from native to Wii
static unsigned int opt_name_mapping[][2] = {
    {SO_REUSEADDR, 0x4}, {SO_SNDBUF, 0x1001}, {SO_RCVBUF, 0x1002}, {SO_ERROR, 0x1009}};

IPCCommandResult NetIPTop::IOCtl(const IOCtlRequest& request)
{
  if (Core::g_want_determinism)
  {
    return GetDefaultReply(IPC_EACCES);
  }

  s32 return_value = 0;
  switch (request.request)
  {
  case IOCTL_SO_STARTUP:
  {
    request.Log(GetDeviceName(), LogTypes::IOS_WC24);
    break;
  }
  case IOCTL_SO_SOCKET:
  {
    u32 af = Memory::Read_U32(request.buffer_in);
    u32 type = Memory::Read_U32(request.buffer_in + 4);
    u32 prot = Memory::Read_U32(request.buffer_in + 8);

    WiiSockMan& sm = WiiSockMan::GetInstance();
    return_value = sm.NewSocket(af, type, prot);
    INFO_LOG(IOS_NET, "IOCTL_SO_SOCKET "
                      "Socket: %08x (%d,%d,%d), BufferIn: (%08x, %i), BufferOut: (%08x, %i)",
             return_value, af, type, prot, request.buffer_in, request.buffer_in_size,
             request.buffer_out, request.buffer_out_size);
    break;
  }
  case IOCTL_SO_ICMPSOCKET:
  {
    u32 pf = Memory::Read_U32(request.buffer_in);

    WiiSockMan& sm = WiiSockMan::GetInstance();
    return_value = sm.NewSocket(pf, SOCK_RAW, IPPROTO_ICMP);
    INFO_LOG(IOS_NET, "IOCTL_SO_ICMPSOCKET(%x) %d", pf, return_value);
    break;
  }
  case IOCTL_SO_CLOSE:
  case IOCTL_SO_ICMPCLOSE:
  {
    u32 fd = Memory::Read_U32(request.buffer_in);
    WiiSockMan& sm = WiiSockMan::GetInstance();
    return_value = sm.DeleteSocket(fd);
    INFO_LOG(IOS_NET, "%s(%x) %x",
             request.request == IOCTL_SO_ICMPCLOSE ? "IOCTL_SO_ICMPCLOSE" : "IOCTL_SO_CLOSE", fd,
             return_value);
    break;
  }
  case IOCTL_SO_ACCEPT:
  case IOCTL_SO_BIND:
  case IOCTL_SO_CONNECT:
  case IOCTL_SO_FCNTL:
  {
    u32 fd = Memory::Read_U32(request.buffer_in);
    WiiSockMan& sm = WiiSockMan::GetInstance();
    sm.DoSock(fd, request, static_cast<NET_IOCTL>(request.request));
    return GetNoReply();
  }
  /////////////////////////////////////////////////////////////
  //                  TODO: Tidy all below                   //
  /////////////////////////////////////////////////////////////
  case IOCTL_SO_SHUTDOWN:
  {
    request.Log(GetDeviceName(), LogTypes::IOS_WC24);

    u32 fd = Memory::Read_U32(request.buffer_in);
    u32 how = Memory::Read_U32(request.buffer_in + 4);
    int ret = shutdown(fd, how);
    return_value = WiiSockMan::GetNetErrorCode(ret, "SO_SHUTDOWN", false);
    break;
  }
  case IOCTL_SO_LISTEN:
  {
    u32 fd = Memory::Read_U32(request.buffer_in);
    u32 BACKLOG = Memory::Read_U32(request.buffer_in + 0x04);
    u32 ret = listen(fd, BACKLOG);
    return_value = WiiSockMan::GetNetErrorCode(ret, "SO_LISTEN", false);
    request.Log(GetDeviceName(), LogTypes::IOS_WC24);
    break;
  }
  case IOCTL_SO_GETSOCKOPT:
  {
    u32 fd = Memory::Read_U32(request.buffer_out);
    u32 level = Memory::Read_U32(request.buffer_out + 4);
    u32 optname = Memory::Read_U32(request.buffer_out + 8);

    request.Log(GetDeviceName(), LogTypes::IOS_WC24);

    // Do the level/optname translation
    int nat_level = -1, nat_optname = -1;

    for (auto& map : opt_level_mapping)
      if (level == map[1])
        nat_level = map[0];

    for (auto& map : opt_name_mapping)
      if (optname == map[1])
        nat_optname = map[0];

    u8 optval[20];
    u32 optlen = 4;

    int ret = getsockopt(fd, nat_level, nat_optname, (char*)&optval, (socklen_t*)&optlen);
    return_value = WiiSockMan::GetNetErrorCode(ret, "SO_GETSOCKOPT", false);

    Memory::Write_U32(optlen, request.buffer_out + 0xC);
    Memory::CopyToEmu(request.buffer_out + 0x10, optval, optlen);

    if (optname == SO_ERROR)
    {
      s32 last_error = WiiSockMan::GetInstance().GetLastNetError();

      Memory::Write_U32(sizeof(s32), request.buffer_out + 0xC);
      Memory::Write_U32(last_error, request.buffer_out + 0x10);
    }
    break;
  }

  case IOCTL_SO_SETSOCKOPT:
  {
    u32 fd = Memory::Read_U32(request.buffer_in);
    u32 level = Memory::Read_U32(request.buffer_in + 4);
    u32 optname = Memory::Read_U32(request.buffer_in + 8);
    u32 optlen = Memory::Read_U32(request.buffer_in + 0xc);
    u8 optval[20];
    optlen = std::min(optlen, (u32)sizeof(optval));
    Memory::CopyFromEmu(optval, request.buffer_in + 0x10, optlen);

    INFO_LOG(IOS_NET, "IOCTL_SO_SETSOCKOPT(%08x, %08x, %08x, %08x) "
                      "BufferIn: (%08x, %i), BufferOut: (%08x, %i)"
                      "%02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx "
                      "%02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx %02hhx",
             fd, level, optname, optlen, request.buffer_in, request.buffer_in_size,
             request.buffer_out, request.buffer_out_size, optval[0], optval[1], optval[2],
             optval[3], optval[4], optval[5], optval[6], optval[7], optval[8], optval[9],
             optval[10], optval[11], optval[12], optval[13], optval[14], optval[15], optval[16],
             optval[17], optval[18], optval[19]);

    // TODO: bug booto about this, 0x2005 most likely timeout related, default value on Wii is ,
    // 0x2001 is most likely tcpnodelay
    if (level == 6 && (optname == 0x2005 || optname == 0x2001))
    {
      return_value = 0;
      break;
    }

    // Do the level/optname translation
    int nat_level = -1, nat_optname = -1;

    for (auto& map : opt_level_mapping)
      if (level == map[1])
        nat_level = map[0];

    for (auto& map : opt_name_mapping)
      if (optname == map[1])
        nat_optname = map[0];

    if (nat_level == -1 || nat_optname == -1)
    {
      INFO_LOG(IOS_NET, "SO_SETSOCKOPT: unknown level %d or optname %d", level, optname);

      // Default to the given level/optname. They match on Windows...
      nat_level = level;
      nat_optname = optname;
    }

    int ret = setsockopt(fd, nat_level, nat_optname, (char*)optval, optlen);
    return_value = WiiSockMan::GetNetErrorCode(ret, "SO_SETSOCKOPT", false);
    break;
  }
  case IOCTL_SO_GETSOCKNAME:
  {
    u32 fd = Memory::Read_U32(request.buffer_in);

    request.Log(GetDeviceName(), LogTypes::IOS_WC24);

    sockaddr sa;
    socklen_t sa_len;
    sa_len = sizeof(sa);
    int ret = getsockname(fd, &sa, &sa_len);

    if (request.buffer_out_size < 2 + sizeof(sa.sa_data))
      WARN_LOG(IOS_NET, "IOCTL_SO_GETSOCKNAME output buffer is too small. Truncating");

    if (request.buffer_out_size > 0)
      Memory::Write_U8(request.buffer_out_size, request.buffer_out);
    if (request.buffer_out_size > 1)
      Memory::Write_U8(sa.sa_family & 0xFF, request.buffer_out + 1);
    if (request.buffer_out_size > 2)
      Memory::CopyToEmu(request.buffer_out + 2, &sa.sa_data,
                        std::min<size_t>(sizeof(sa.sa_data), request.buffer_out_size - 2));
    return_value = ret;
    break;
  }
  case IOCTL_SO_GETPEERNAME:
  {
    u32 fd = Memory::Read_U32(request.buffer_in);

    sockaddr sa;
    socklen_t sa_len;
    sa_len = sizeof(sa);

    int ret = getpeername(fd, &sa, &sa_len);

    if (request.buffer_out_size < 2 + sizeof(sa.sa_data))
      WARN_LOG(IOS_NET, "IOCTL_SO_GETPEERNAME output buffer is too small. Truncating");

    if (request.buffer_out_size > 0)
      Memory::Write_U8(request.buffer_out_size, request.buffer_out);
    if (request.buffer_out_size > 1)
      Memory::Write_U8(AF_INET, request.buffer_out + 1);
    if (request.buffer_out_size > 2)
      Memory::CopyToEmu(request.buffer_out + 2, &sa.sa_data,
                        std::min<size_t>(sizeof(sa.sa_data), request.buffer_out_size - 2));

    INFO_LOG(IOS_NET, "IOCTL_SO_GETPEERNAME(%x)", fd);

    return_value = ret;
    break;
  }

  case IOCTL_SO_GETHOSTID:
  {
    request.Log(GetDeviceName(), LogTypes::IOS_WC24);

#ifdef _WIN32
    DWORD forwardTableSize, ipTableSize, result;
    DWORD ifIndex = -1;
    std::unique_ptr<MIB_IPFORWARDTABLE> forwardTable;
    std::unique_ptr<MIB_IPADDRTABLE> ipTable;

    forwardTableSize = 0;
    if (GetIpForwardTable(nullptr, &forwardTableSize, FALSE) == ERROR_INSUFFICIENT_BUFFER)
    {
      forwardTable =
          std::unique_ptr<MIB_IPFORWARDTABLE>((PMIB_IPFORWARDTABLE) operator new(forwardTableSize));
    }

    ipTableSize = 0;
    if (GetIpAddrTable(nullptr, &ipTableSize, FALSE) == ERROR_INSUFFICIENT_BUFFER)
    {
      ipTable = std::unique_ptr<MIB_IPADDRTABLE>((PMIB_IPADDRTABLE) operator new(ipTableSize));
    }

    // find the interface IP used for the default route and use that
    result = GetIpForwardTable(forwardTable.get(), &forwardTableSize, FALSE);
    while (result == NO_ERROR ||
           result == ERROR_MORE_DATA)  // can return ERROR_MORE_DATA on XP even after the first call
    {
      for (DWORD i = 0; i < forwardTable->dwNumEntries; ++i)
      {
        if (forwardTable->table[i].dwForwardDest == 0)
        {
          ifIndex = forwardTable->table[i].dwForwardIfIndex;
          break;
        }
      }

      if (result == NO_ERROR || ifIndex != -1)
        break;

      result = GetIpForwardTable(forwardTable.get(), &forwardTableSize, FALSE);
    }

    if (ifIndex != -1 && GetIpAddrTable(ipTable.get(), &ipTableSize, FALSE) == NO_ERROR)
    {
      for (DWORD i = 0; i < ipTable->dwNumEntries; ++i)
      {
        if (ipTable->table[i].dwIndex == ifIndex)
        {
          return_value = Common::swap32(ipTable->table[i].dwAddr);
          break;
        }
      }
    }
#endif

    // default placeholder, in case of failure
    if (return_value == 0)
      return_value = 192 << 24 | 168 << 16 | 1 << 8 | 150;
    break;
  }

  case IOCTL_SO_INETATON:
  {
    std::string hostname = Memory::GetString(request.buffer_in);
    struct hostent* remoteHost = gethostbyname(hostname.c_str());

    if (remoteHost == nullptr || remoteHost->h_addr_list == nullptr ||
        remoteHost->h_addr_list[0] == nullptr)
    {
      INFO_LOG(IOS_NET, "IOCTL_SO_INETATON = -1 "
                        "%s, BufferIn: (%08x, %i), BufferOut: (%08x, %i), IP Found: None",
               hostname.c_str(), request.buffer_in, request.buffer_in_size, request.buffer_out,
               request.buffer_out_size);
      return_value = 0;
    }
    else
    {
      Memory::Write_U32(Common::swap32(*(u32*)remoteHost->h_addr_list[0]), request.buffer_out);
      INFO_LOG(IOS_NET, "IOCTL_SO_INETATON = 0 "
                        "%s, BufferIn: (%08x, %i), BufferOut: (%08x, %i), IP Found: %08X",
               hostname.c_str(), request.buffer_in, request.buffer_in_size, request.buffer_out,
               request.buffer_out_size, Common::swap32(*(u32*)remoteHost->h_addr_list[0]));
      return_value = 1;
    }
    break;
  }

  case IOCTL_SO_INETPTON:
  {
    std::string address = Memory::GetString(request.buffer_in);
    INFO_LOG(IOS_NET, "IOCTL_SO_INETPTON "
                      "(Translating: %s)",
             address.c_str());
    return_value = inet_pton(address.c_str(), Memory::GetPointer(request.buffer_out + 4));
    break;
  }

  case IOCTL_SO_INETNTOP:
  {
    // u32 af = Memory::Read_U32(BufferIn);
    // u32 validAddress = Memory::Read_U32(request.buffer_in + 4);
    // u32 src = Memory::Read_U32(request.buffer_in + 8);
    char ip_s[16];
    sprintf(ip_s, "%i.%i.%i.%i", Memory::Read_U8(request.buffer_in + 8),
            Memory::Read_U8(request.buffer_in + 8 + 1), Memory::Read_U8(request.buffer_in + 8 + 2),
            Memory::Read_U8(request.buffer_in + 8 + 3));
    INFO_LOG(IOS_NET, "IOCTL_SO_INETNTOP %s", ip_s);
    Memory::CopyToEmu(request.buffer_out, (u8*)ip_s, strlen(ip_s));
    break;
  }

  case IOCTL_SO_POLL:
  {
    // Map Wii/native poll events types
    struct
    {
      int native;
      int wii;
    } mapping[] = {
        {POLLRDNORM, 0x0001}, {POLLRDBAND, 0x0002}, {POLLPRI, 0x0004}, {POLLWRNORM, 0x0008},
        {POLLWRBAND, 0x0010}, {POLLERR, 0x0020},    {POLLHUP, 0x0040}, {POLLNVAL, 0x0080},
    };

    u32 unknown = Memory::Read_U32(request.buffer_in);
    u32 timeout = Memory::Read_U32(request.buffer_in + 4);

    int nfds = request.buffer_out_size / 0xc;
    if (nfds == 0)
      ERROR_LOG(IOS_NET, "Hidden POLL");

    std::vector<pollfd_t> ufds(nfds);

    for (int i = 0; i < nfds; ++i)
    {
      ufds[i].fd = Memory::Read_U32(request.buffer_out + 0xc * i);           // fd
      int events = Memory::Read_U32(request.buffer_out + 0xc * i + 4);       // events
      ufds[i].revents = Memory::Read_U32(request.buffer_out + 0xc * i + 8);  // revents

      // Translate Wii to native events
      int unhandled_events = events;
      ufds[i].events = 0;
      for (auto& map : mapping)
      {
        if (events & map.wii)
          ufds[i].events |= map.native;
        unhandled_events &= ~map.wii;
      }
      DEBUG_LOG(IOS_NET, "IOCTL_SO_POLL(%d) "
                         "Sock: %08x, Unknown: %08x, Events: %08x, "
                         "NativeEvents: %08x",
                i, ufds[i].fd, unknown, events, ufds[i].events);

      // Do not pass return-only events to the native poll
      ufds[i].events &= ~(POLLERR | POLLHUP | POLLNVAL | UNSUPPORTED_WSAPOLL);

      if (unhandled_events)
        ERROR_LOG(IOS_NET, "SO_POLL: unhandled Wii event types: %04x", unhandled_events);
    }

    int ret = poll(ufds.data(), nfds, timeout);
    ret = WiiSockMan::GetNetErrorCode(ret, "SO_POLL", false);

    for (int i = 0; i < nfds; ++i)
    {
      // Translate native to Wii events
      int revents = 0;
      for (auto& map : mapping)
      {
        if (ufds[i].revents & map.native)
          revents |= map.wii;
      }

      // No need to change fd or events as they are input only.
      // Memory::Write_U32(ufds[i].fd, request.buffer_out + 0xc*i); //fd
      // Memory::Write_U32(events, request.buffer_out + 0xc*i + 4); //events
      Memory::Write_U32(revents, request.buffer_out + 0xc * i + 8);  // revents

      DEBUG_LOG(IOS_NET, "IOCTL_SO_POLL socket %d wevents %08X events %08X revents %08X", i,
                revents, ufds[i].events, ufds[i].revents);
    }

    return_value = ret;
    break;
  }

  case IOCTL_SO_GETHOSTBYNAME:
  {
    if (request.buffer_out_size != 0x460)
    {
      ERROR_LOG(IOS_NET, "Bad buffer size for IOCTL_SO_GETHOSTBYNAME");
      return_value = -1;
      break;
    }

    std::string hostname = Memory::GetString(request.buffer_in);
    hostent* remoteHost = gethostbyname(hostname.c_str());

    INFO_LOG(IOS_NET, "IOCTL_SO_GETHOSTBYNAME "
                      "Address: %s, BufferIn: (%08x, %i), BufferOut: (%08x, %i)",
             hostname.c_str(), request.buffer_in, request.buffer_in_size, request.buffer_out,
             request.buffer_out_size);

    if (remoteHost)
    {
      for (int i = 0; remoteHost->h_aliases[i]; ++i)
      {
        DEBUG_LOG(IOS_NET, "alias%i:%s", i, remoteHost->h_aliases[i]);
      }

      for (int i = 0; remoteHost->h_addr_list[i]; ++i)
      {
        u32 ip = Common::swap32(*(u32*)(remoteHost->h_addr_list[i]));
        std::string ip_s = StringFromFormat("%i.%i.%i.%i", ip >> 24, (ip >> 16) & 0xff,
                                            (ip >> 8) & 0xff, ip & 0xff);
        DEBUG_LOG(IOS_NET, "addr%i:%s", i, ip_s.c_str());
      }

      // Host name; located immediately after struct
      static const u32 GETHOSTBYNAME_STRUCT_SIZE = 0x10;
      static const u32 GETHOSTBYNAME_IP_LIST_OFFSET = 0x110;
      // Limit host name length to avoid buffer overflow.
      u32 name_length = (u32)strlen(remoteHost->h_name) + 1;
      if (name_length > (GETHOSTBYNAME_IP_LIST_OFFSET - GETHOSTBYNAME_STRUCT_SIZE))
      {
        ERROR_LOG(IOS_NET, "Hostname too long in IOCTL_SO_GETHOSTBYNAME");
        return_value = -1;
        break;
      }
      Memory::CopyToEmu(request.buffer_out + GETHOSTBYNAME_STRUCT_SIZE, remoteHost->h_name,
                        name_length);
      Memory::Write_U32(request.buffer_out + GETHOSTBYNAME_STRUCT_SIZE, request.buffer_out);

      // IP address list; located at offset 0x110.
      u32 num_ip_addr = 0;
      while (remoteHost->h_addr_list[num_ip_addr])
        num_ip_addr++;
      // Limit number of IP addresses to avoid buffer overflow.
      // (0x460 - 0x340) / sizeof(pointer) == 72
      static const u32 GETHOSTBYNAME_MAX_ADDRESSES = 71;
      num_ip_addr = std::min(num_ip_addr, GETHOSTBYNAME_MAX_ADDRESSES);
      for (u32 i = 0; i < num_ip_addr; ++i)
      {
        u32 addr = request.buffer_out + GETHOSTBYNAME_IP_LIST_OFFSET + i * 4;
        Memory::Write_U32_Swap(*(u32*)(remoteHost->h_addr_list[i]), addr);
      }

      // List of pointers to IP addresses; located at offset 0x340.
      // This must be exact: PPC code to convert the struct hardcodes
      // this offset.
      static const u32 GETHOSTBYNAME_IP_PTR_LIST_OFFSET = 0x340;
      Memory::Write_U32(request.buffer_out + GETHOSTBYNAME_IP_PTR_LIST_OFFSET,
                        request.buffer_out + 12);
      for (u32 i = 0; i < num_ip_addr; ++i)
      {
        u32 addr = request.buffer_out + GETHOSTBYNAME_IP_PTR_LIST_OFFSET + i * 4;
        Memory::Write_U32(request.buffer_out + GETHOSTBYNAME_IP_LIST_OFFSET + i * 4, addr);
      }
      Memory::Write_U32(0, request.buffer_out + GETHOSTBYNAME_IP_PTR_LIST_OFFSET + num_ip_addr * 4);

      // Aliases - empty. (Hardware doesn't return anything.)
      Memory::Write_U32(request.buffer_out + GETHOSTBYNAME_IP_PTR_LIST_OFFSET + num_ip_addr * 4,
                        request.buffer_out + 4);

      // Returned struct must be ipv4.
      _assert_msg_(IOS_NET,
                   remoteHost->h_addrtype == AF_INET && remoteHost->h_length == sizeof(u32),
                   "returned host info is not IPv4");
      Memory::Write_U16(AF_INET, request.buffer_out + 8);
      Memory::Write_U16(sizeof(u32), request.buffer_out + 10);

      return_value = 0;
    }
    else
    {
      return_value = -1;
    }

    break;
  }

  case IOCTL_SO_ICMPCANCEL:
    ERROR_LOG(IOS_NET, "IOCTL_SO_ICMPCANCEL");

  default:
    request.DumpUnknown(GetDeviceName(), LogTypes::IOS_NET);
  }

  return GetDefaultReply(return_value);
}

IPCCommandResult NetIPTop::IOCtlV(const IOCtlVRequest& request)
{
  s32 return_value = 0;

  u32 param = 0, param2 = 0, param3, param4, param5 = 0;
  switch (request.request)
  {
  case IOCTLV_SO_GETINTERFACEOPT:
  {
    param = Memory::Read_U32(request.in_vectors[0].address);
    param2 = Memory::Read_U32(request.in_vectors[0].address + 4);
    param3 = Memory::Read_U32(request.io_vectors[0].address);
    param4 = Memory::Read_U32(request.io_vectors[1].address);
    if (request.io_vectors[0].size >= 8)
    {
      param5 = Memory::Read_U32(request.io_vectors[0].address + 4);
    }

    INFO_LOG(IOS_NET, "IOCTLV_SO_GETINTERFACEOPT(%08X, %08X, %X, %X, %X) "
                      "BufferIn: (%08x, %i), BufferIn2: (%08x, %i) ",
             param, param2, param3, param4, param5, request.in_vectors[0].address,
             request.in_vectors[0].size,
             request.in_vectors.size() > 1 ? request.in_vectors[1].address : 0,
             request.in_vectors.size() > 1 ? request.in_vectors[1].size : 0);

    switch (param2)
    {
    case 0xb003:  // dns server table
    {
      u32 address = 0;
#ifdef _WIN32
      if (!Core::g_want_determinism)
      {
        PIP_ADAPTER_ADDRESSES AdapterAddresses = nullptr;
        ULONG OutBufferLength = 0;
        ULONG RetVal = 0, i;
        for (i = 0; i < 5; ++i)
        {
          RetVal = GetAdaptersAddresses(AF_INET, 0, nullptr, AdapterAddresses, &OutBufferLength);

          if (RetVal != ERROR_BUFFER_OVERFLOW)
          {
            break;
          }

          if (AdapterAddresses != nullptr)
          {
            FREE(AdapterAddresses);
          }

          AdapterAddresses = (PIP_ADAPTER_ADDRESSES)MALLOC(OutBufferLength);
          if (AdapterAddresses == nullptr)
          {
            RetVal = GetLastError();
            break;
          }
        }
        if (RetVal == NO_ERROR)
        {
          unsigned long dwBestIfIndex = 0;
          IPAddr dwDestAddr = (IPAddr)0x08080808;
          // If successful, output some information from the data we received
          PIP_ADAPTER_ADDRESSES AdapterList = AdapterAddresses;
          if (GetBestInterface(dwDestAddr, &dwBestIfIndex) == NO_ERROR)
          {
            while (AdapterList)
            {
              if (AdapterList->IfIndex == dwBestIfIndex && AdapterList->FirstDnsServerAddress &&
                  AdapterList->OperStatus == IfOperStatusUp)
              {
                INFO_LOG(IOS_NET, "Name of valid interface: %S", AdapterList->FriendlyName);
                INFO_LOG(IOS_NET, "DNS: %u.%u.%u.%u",
                         (unsigned char)
                             AdapterList->FirstDnsServerAddress->Address.lpSockaddr->sa_data[2],
                         (unsigned char)
                             AdapterList->FirstDnsServerAddress->Address.lpSockaddr->sa_data[3],
                         (unsigned char)
                             AdapterList->FirstDnsServerAddress->Address.lpSockaddr->sa_data[4],
                         (unsigned char)
                             AdapterList->FirstDnsServerAddress->Address.lpSockaddr->sa_data[5]);
                address = Common::swap32(
                    *(u32*)(&AdapterList->FirstDnsServerAddress->Address.lpSockaddr->sa_data[2]));
                break;
              }
              AdapterList = AdapterList->Next;
            }
          }
        }
        if (AdapterAddresses != nullptr)
        {
          FREE(AdapterAddresses);
        }
      }
#endif
      if (address == 0)
        address = 0x08080808;

      Memory::Write_U32(address, request.io_vectors[0].address);
      Memory::Write_U32(0x08080404, request.io_vectors[0].address + 4);
      break;
    }
    case 0x1003:  // error
      Memory::Write_U32(0, request.io_vectors[0].address);
      break;

    case 0x1004:  // mac address
      u8 address[Common::MAC_ADDRESS_SIZE];
      GetMacAddress(address);
      Memory::CopyToEmu(request.io_vectors[0].address, address, sizeof(address));
      break;

    case 0x1005:  // link state
      Memory::Write_U32(1, request.io_vectors[0].address);
      break;

    case 0x4002:  // ip addr number
      Memory::Write_U32(1, request.io_vectors[0].address);
      break;

    case 0x4003:  // ip addr table
      Memory::Write_U32(0xC, request.io_vectors[1].address);
      Memory::Write_U32(10 << 24 | 1 << 8 | 30, request.io_vectors[0].address);
      Memory::Write_U32(255 << 24 | 255 << 16 | 255 << 8 | 0, request.io_vectors[0].address + 4);
      Memory::Write_U32(10 << 24 | 0 << 16 | 255 << 8 | 255, request.io_vectors[0].address + 8);
      break;

    default:
      ERROR_LOG(IOS_NET, "Unknown param2: %08X", param2);
      break;
    }
    break;
  }
  case IOCTLV_SO_SENDTO:
  {
    u32 fd = Memory::Read_U32(request.in_vectors[1].address);
    WiiSockMan& sm = WiiSockMan::GetInstance();
    sm.DoSock(fd, request, IOCTLV_SO_SENDTO);
    return GetNoReply();
    break;
  }
  case IOCTLV_SO_RECVFROM:
  {
    u32 fd = Memory::Read_U32(request.in_vectors[0].address);
    WiiSockMan& sm = WiiSockMan::GetInstance();
    sm.DoSock(fd, request, IOCTLV_SO_RECVFROM);
    return GetNoReply();
    break;
  }
  case IOCTLV_SO_GETADDRINFO:
  {
    addrinfo hints;

    if (request.in_vectors.size() > 2 && request.in_vectors[2].size)
    {
      hints.ai_flags = Memory::Read_U32(request.in_vectors[2].address);
      hints.ai_family = Memory::Read_U32(request.in_vectors[2].address + 0x4);
      hints.ai_socktype = Memory::Read_U32(request.in_vectors[2].address + 0x8);
      hints.ai_protocol = Memory::Read_U32(request.in_vectors[2].address + 0xC);
      hints.ai_addrlen = Memory::Read_U32(request.in_vectors[2].address + 0x10);
      hints.ai_canonname = nullptr;
      hints.ai_addr = nullptr;
      hints.ai_next = nullptr;
    }

    // getaddrinfo allows a null pointer for the nodeName or serviceName strings
    // So we have to do a bit of juggling here.
    std::string nodeNameStr;
    const char* pNodeName = nullptr;
    if (request.in_vectors.size() > 0 && request.in_vectors[0].size > 0)
    {
      nodeNameStr = Memory::GetString(request.in_vectors[0].address, request.in_vectors[0].size);
      pNodeName = nodeNameStr.c_str();
    }

    std::string serviceNameStr;
    const char* pServiceName = nullptr;
    if (request.in_vectors.size() > 1 && request.in_vectors[1].size > 0)
    {
      serviceNameStr = Memory::GetString(request.in_vectors[1].address, request.in_vectors[1].size);
      pServiceName = serviceNameStr.c_str();
    }

    addrinfo* result = nullptr;
    int ret = getaddrinfo(
        pNodeName, pServiceName,
        (request.in_vectors.size() > 2 && request.in_vectors[2].size) ? &hints : nullptr, &result);
    u32 addr = request.io_vectors[0].address;
    u32 sockoffset = addr + 0x460;
    if (ret == 0)
    {
      for (addrinfo* result_iter = result; result_iter != nullptr;
           result_iter = result_iter->ai_next)
      {
        Memory::Write_U32(result_iter->ai_flags, addr);
        Memory::Write_U32(result_iter->ai_family, addr + 0x04);
        Memory::Write_U32(result_iter->ai_socktype, addr + 0x08);
        Memory::Write_U32(result_iter->ai_protocol, addr + 0x0C);
        Memory::Write_U32((u32)result_iter->ai_addrlen, addr + 0x10);
        // what to do? where to put? the buffer of 0x834 doesn't allow space for this
        Memory::Write_U32(/*result->ai_cannonname*/ 0, addr + 0x14);

        if (result_iter->ai_addr)
        {
          Memory::Write_U32(sockoffset, addr + 0x18);
          Memory::Write_U16(((result_iter->ai_addr->sa_family & 0xFF) << 8) |
                                (result_iter->ai_addrlen & 0xFF),
                            sockoffset);
          Memory::CopyToEmu(sockoffset + 0x2, result_iter->ai_addr->sa_data,
                            sizeof(result_iter->ai_addr->sa_data));
          sockoffset += 0x1C;
        }
        else
        {
          Memory::Write_U32(0, addr + 0x18);
        }

        if (result_iter->ai_next)
        {
          Memory::Write_U32(addr + sizeof(addrinfo), addr + 0x1C);
        }
        else
        {
          Memory::Write_U32(0, addr + 0x1C);
        }

        addr += sizeof(addrinfo);
      }

      freeaddrinfo(result);
    }
    else
    {
      // Host not found
      ret = -305;
    }

    request.Dump(GetDeviceName(), LogTypes::IOS_NET, LogTypes::LINFO);
    return_value = ret;
    break;
  }
  case IOCTLV_SO_ICMPPING:
  {
    struct
    {
      u8 length;
      u8 addr_family;
      u16 icmp_id;
      u32 ip;
    } ip_info;

    u32 fd = Memory::Read_U32(request.in_vectors[0].address);
    u32 num_ip = Memory::Read_U32(request.in_vectors[0].address + 4);
    u64 timeout = Memory::Read_U64(request.in_vectors[0].address + 8);

    if (num_ip != 1)
    {
      INFO_LOG(IOS_NET, "IOCTLV_SO_ICMPPING %i IPs", num_ip);
    }

    ip_info.length = Memory::Read_U8(request.in_vectors[0].address + 16);
    ip_info.addr_family = Memory::Read_U8(request.in_vectors[0].address + 17);
    ip_info.icmp_id = Memory::Read_U16(request.in_vectors[0].address + 18);
    ip_info.ip = Memory::Read_U32(request.in_vectors[0].address + 20);

    if (ip_info.length != 8 || ip_info.addr_family != AF_INET)
    {
      INFO_LOG(IOS_NET, "IOCTLV_SO_ICMPPING strange IPInfo:\n"
                        "length %x addr_family %x",
               ip_info.length, ip_info.addr_family);
    }

    INFO_LOG(IOS_NET, "IOCTLV_SO_ICMPPING %x", ip_info.ip);

    sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = Common::swap32(ip_info.ip);
    memset(addr.sin_zero, 0, 8);

    u8 data[0x20];
    memset(data, 0, sizeof(data));
    s32 icmp_length = sizeof(data);

    if (request.in_vectors.size() > 1 && request.in_vectors[1].size == sizeof(data))
      Memory::CopyFromEmu(data, request.in_vectors[1].address, request.in_vectors[1].size);
    else
    {
      // TODO sequence number is incremented either statically, by
      // port, or by socket. Doesn't seem to matter, so we just leave
      // it 0
      ((u16*)data)[0] = Common::swap16(ip_info.icmp_id);
      icmp_length = 22;
    }

    int ret = icmp_echo_req(fd, &addr, data, icmp_length);
    if (ret == icmp_length)
    {
      ret = icmp_echo_rep(fd, &addr, (u32)timeout, icmp_length);
    }

    // TODO proper error codes
    return_value = 0;
    break;
  }
  default:
    request.DumpUnknown(GetDeviceName(), LogTypes::IOS_NET);
  }

  return GetDefaultReply(return_value);
}

void NetIPTop::Update()
{
  WiiSockMan::GetInstance().Update();
}
}  // namespace Device
}  // namespace HLE
}  // namespace IOS
