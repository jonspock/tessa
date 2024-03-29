// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "coin-config.h"
#endif

#include "net.h"
#include "wallet_externs.h"

#include "addrman.h"
#include "chainparams.h"
#include "clientversion.h"
#include "coin_constants.h"  // REJECT message codes
#include "fs.h"
#include "fs_utils.h"
#include "main_functions.h"  // For ActiveProtocol
#include "miner.h"
#include "primitives/transaction.h"
#include "scheduler.h"
#include "ui_interface.h"
#include "utiltime.h"
#include "wallet/wallet.h"
#include "wallet/wallettx.h"

#include "json/json.hpp"

#ifdef WIN32
#include <string.h>
#else
#include <fcntl.h>
#endif

#ifdef USE_UPNP
#include <miniupnpc/miniupnpc.h>
#include <miniupnpc/miniwget.h>
#include <miniupnpc/upnpcommands.h>
#include <miniupnpc/upnperrors.h>
#endif

#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>

// Dump addresses to peers.dat every 15 minutes (900s)
#define DUMP_ADDRESSES_INTERVAL 900

#if !defined(HAVE_MSG_NOSIGNAL) && !defined(MSG_NOSIGNAL)
#define MSG_NOSIGNAL 0
#endif

// Fix for ancient MinGW versions, that don't have defined these in ws2tcpip.h.
// Todo: Can be removed when our pull-tester is upgraded to a modern MinGW version.
#ifdef WIN32
#ifndef PROTECTION_LEVEL_UNRESTRICTED
#define PROTECTION_LEVEL_UNRESTRICTED 10
#endif
#ifndef IPV6_PROTECTION_LEVEL
#define IPV6_PROTECTION_LEVEL 23
#endif
#endif

using namespace std;

namespace {
const int MAX_OUTBOUND_CONNECTIONS = 16;

struct ListenSocket {
  SOCKET socket;
  bool whitelisted;

  ListenSocket(SOCKET socket, bool whitelisted) : socket(socket), whitelisted(whitelisted) {}
};
}  // namespace

//
// Global state variables
//
bool fDiscover = true;
bool fListen = true;
uint64_t nLocalServices = NODE_NETWORK;
CCriticalSection cs_mapLocalHost;
map<CNetAddr, LocalServiceInfo> mapLocalHost;
static bool vfLimited[NET_MAX] = {};
static CNode* pnodeLocalHost = nullptr;
uint64_t nLocalHostNonce = 0;
static std::vector<ListenSocket> vhListenSocket;
CAddrMan addrman;
int nMaxConnections = 125;
bool fAddressesInitialized = false;

vector<CNode*> vNodes;
CCriticalSection cs_vNodes;
map<CInv, CDataStream> mapRelay;
deque<pair<int64_t, CInv> > vRelayExpiration;
CCriticalSection cs_mapRelay;
limitedmap<CInv, int64_t> mapAlreadyAskedFor(MAX_INV_SZ);

static deque<string> vOneShots;
CCriticalSection cs_vOneShots;

set<CNetAddr> setservAddNodeAddresses;
CCriticalSection cs_setservAddNodeAddresses;

vector<std::string> vAddedNodes;
CCriticalSection cs_vAddedNodes;

NodeId nLastNodeId = 0;
CCriticalSection cs_nLastNodeId;

static CSemaphore* semOutbound = nullptr;
std::condition_variable messageHandlerCondition;

// Signals for message handling
static CNodeSignals g_signals;
CNodeSignals& GetNodeSignals() { return g_signals; }

static std::condition_variable net_interrupt_cond;
static std::mutex cs_net_interrupt;
static std::atomic<bool> net_interrupted(false);

std::thread dns_address_seed_thread;
std::thread socket_handler_thread;
std::thread open_added_connections_thread;
std::thread open_connections_thread;
std::thread message_handler_thread;
std::thread staking_handler_thread;

// 2 Classes here just used in this file

/** Access to the (IP) address database (peers.dat) */
class CAddrDB {
 private:
  fs::path pathAddr;

 public:
  CAddrDB();
  bool Write(const CAddrMan& addr);
  bool Read(CAddrMan& addr);
};

/** Access to the banlist database (banlist.dat) */
class CBanDB {
 private:
  fs::path pathBanlist;

 public:
  CBanDB();
  bool Write(const banmap_t& banSet);
  bool Read(banmap_t& banSet);
};

static void InterruptibleSleep(uint64_t n) {
  bool ret = false;
  {
    std::unique_lock<std::mutex> lock(cs_net_interrupt);
    ret = net_interrupt_cond.wait_for(lock, std::chrono::milliseconds(n), []() -> bool { return net_interrupted; });
  }
  interruption_point(ret);
}

void AddOneShot(string strDest) {
  LOCK(cs_vOneShots);
  vOneShots.push_back(strDest);
}

uint16_t GetListenPort() { return (uint16_t)(GetArg("-port", Params().GetDefaultPort())); }

// find 'best' local address for a particular peer
bool GetLocal(CService& addr, const CNetAddr* paddrPeer) {
  if (!fListen) return false;

  int nBestScore = -1;
  int nBestReachability = -1;
  {
    LOCK(cs_mapLocalHost);
    for (const auto& it : mapLocalHost) {
      int nScore = it.second.nScore;
      int nReachability = it.first.GetReachabilityFrom(paddrPeer);
      if (nReachability > nBestReachability || (nReachability == nBestReachability && nScore > nBestScore)) {
        addr = CService(it.first, it.second.nPort);
        nBestReachability = nReachability;
        nBestScore = nScore;
      }
    }
  }
  return nBestScore >= 0;
}

// get best local address for a particular peer as a CAddress
// Otherwise, return the unroutable 0.0.0.0 but filled in with
// the normal parameters, since the IP may be changed to a useful
// one by discovery.
CAddress GetLocalAddress(const CNetAddr* paddrPeer) {
  CAddress ret(CService("0.0.0.0", GetListenPort()), 0);
  CService addr;
  if (GetLocal(addr, paddrPeer)) { ret = CAddress(addr); }
  ret.nServices = nLocalServices;
  ret.nTime = GetAdjustedTime();
  return ret;
}

bool RecvLine(SOCKET hSocket, string& strLine) {
  strLine = "";
  while (true) {
    char c;
    int nBytes = recv(hSocket, &c, 1, 0);
    if (nBytes > 0) {
      if (c == '\n') continue;
      if (c == '\r') return true;
      strLine += c;
      if (strLine.size() >= 9000) return true;
    } else if (nBytes <= 0) {
      interruption_point(net_interrupted);
      if (nBytes < 0) {
        int nErr = WSAGetLastError();
        if (nErr == WSAEMSGSIZE) continue;
        if (nErr == WSAEWOULDBLOCK || nErr == WSAEINTR || nErr == WSAEINPROGRESS) {
          MilliSleep(10);
          continue;
        }
      }
      if (!strLine.empty()) return true;
      if (nBytes == 0) {
        // socket closed
        LogPrint(TessaLog::NET, "socket closed\n");
        return false;
      } else {
        // socket error
        int nErr = WSAGetLastError();
        LogPrint(TessaLog::NET, "recv failed: %s\n", NetworkErrorString(nErr));
        return false;
      }
    }
  }
}

int GetnScore(const CService& addr) {
  LOCK(cs_mapLocalHost);
  if (mapLocalHost.count(addr) == LOCAL_NONE) return 0;
  return mapLocalHost[addr].nScore;
}

// Is our peer's addrLocal potentially useful as an external IP source?
bool IsPeerAddrLocalGood(CNode* pnode) {
  return fDiscover && pnode->addr.IsRoutable() && pnode->addrLocal.IsRoutable() &&
         !IsLimited(pnode->addrLocal.GetNetwork());
}

// pushes our own address to a peer
void AdvertizeLocal(CNode* pnode) {
  if (fListen && pnode->fSuccessfullyConnected) {
    CAddress addrLocal = GetLocalAddress(&pnode->addr);
    // If discovery is enabled, sometimes give our peer the address it
    // tells us that it sees us as in case it has a better idea of our
    // address than we do.
    if (IsPeerAddrLocalGood(pnode) &&
        (!addrLocal.IsRoutable() || GetRand((GetnScore(addrLocal) > LOCAL_MANUAL) ? 8 : 2) == 0)) {
      addrLocal.SetIP(pnode->addrLocal);
    }
    if (addrLocal.IsRoutable()) {
      LogPrint(TessaLog::NET, "AdvertizeLocal: advertizing address %s\n", addrLocal.ToString());
      FastRandomContext insecure_rand;
      pnode->PushAddress(addrLocal, insecure_rand);
    }
  }
}

// learn a new local address
bool AddLocal(const CService& addr, int nScore) {
  if (!addr.IsRoutable()) return false;

  if (!fDiscover && nScore < LOCAL_MANUAL) return false;

  if (IsLimited(addr)) return false;

  LogPrint(TessaLog::NET, "AddLocal(%s,%i)\n", addr.ToString(), nScore);

  {
    LOCK(cs_mapLocalHost);
    bool fAlready = mapLocalHost.count(addr) > 0;
    LocalServiceInfo& info = mapLocalHost[addr];
    if (!fAlready || nScore >= info.nScore) {
      info.nScore = nScore + (fAlready ? 1 : 0);
      info.nPort = addr.GetPort();
    }
  }

  return true;
}

bool AddLocal(const CNetAddr& addr, int nScore) { return AddLocal(CService(addr, GetListenPort()), nScore); }

bool RemoveLocal(const CService& addr) {
  LOCK(cs_mapLocalHost);
  LogPrint(TessaLog::NET, "RemoveLocal(%s)\n", addr.ToString());
  mapLocalHost.erase(addr);
  return true;
}

/** Make a particular network entirely off-limits (no automatic connects to it) */
void SetLimited(enum Network net, bool fLimited) {
  if (net == NET_UNROUTABLE) return;
  LOCK(cs_mapLocalHost);
  vfLimited[net] = fLimited;
}

bool IsLimited(enum Network net) {
  LOCK(cs_mapLocalHost);
  return vfLimited[net];
}

bool IsLimited(const CNetAddr& addr) { return IsLimited(addr.GetNetwork()); }

/** vote for a local address */
bool SeenLocal(const CService& addr) {
  {
    LOCK(cs_mapLocalHost);
    if (mapLocalHost.count(addr) == 0) return false;
    mapLocalHost[addr].nScore++;
  }
  return true;
}

/** check whether a given address is potentially local */
bool IsLocal(const CService& addr) {
  LOCK(cs_mapLocalHost);
  return mapLocalHost.count(addr) > 0;
}

/** check whether a given network is one we can probably connect to */
bool IsReachable(enum Network net) {
  LOCK(cs_mapLocalHost);
  return !vfLimited[net];
}

/** check whether a given address is in a network we can probably connect to */
bool IsReachable(const CNetAddr& addr) {
  enum Network net = addr.GetNetwork();
  return IsReachable(net);
}

void AddressCurrentlyConnected(const CService& addr) { addrman.Connected(addr); }

uint64_t CNode::nTotalBytesRecv = 0;
uint64_t CNode::nTotalBytesSent = 0;
CCriticalSection CNode::cs_totalBytesRecv;
CCriticalSection CNode::cs_totalBytesSent;

CNode* FindNode(const CNetAddr& ip) {
  LOCK(cs_vNodes);
  for (CNode* pnode : vNodes)
    if ((CNetAddr)pnode->addr == ip) return (pnode);
  return nullptr;
}

CNode* FindNode(const CSubNet& subNet) {
  LOCK(cs_vNodes);
  for (CNode* pnode : vNodes)
    if (subNet.Match((CNetAddr)pnode->addr)) return (pnode);
  return nullptr;
}

CNode* FindNode(const std::string& addrName) {
  LOCK(cs_vNodes);
  for (CNode* pnode : vNodes)
    if (pnode->addrName == addrName) return (pnode);
  return nullptr;
}

CNode* FindNode(const CService& addr) {
  LOCK(cs_vNodes);
  for (CNode* pnode : vNodes) {
    if (Params().NetworkID() == CBaseChainParams::REGTEST) {
      // if using regtest, just check the IP
      if ((CNetAddr)pnode->addr == (CNetAddr)addr) return (pnode);
    } else {
      if (pnode->addr == addr) return (pnode);
    }
  }
  return nullptr;
}

CNode* ConnectNode(CAddress addrConnect, const char* pszDest, bool obfuScationMaster) {
  if (pszDest == nullptr) {
    // we clean masternode connections in CMasternodeMan::ProcessMasternodeConnections()
    // so should be safe to skip this and connect to local Hot MN on CActiveMasternode::ManageStatus()
    if (IsLocal(addrConnect)) return nullptr;

    // Look for an existing connection
    CNode* pnode = FindNode((CService)addrConnect);
    if (pnode) {
      pnode->AddRef();
      return pnode;
    }
  }

  /// debug print
  LogPrint(TessaLog::NET, "trying connection %s lastseen=%.1fhrs\n", pszDest ? pszDest : addrConnect.ToString(),
           pszDest ? 0.0 : (double)(GetAdjustedTime() - addrConnect.nTime) / 3600.0);

  // Connect
  SOCKET hSocket;
  bool proxyConnectionFailed = false;
  if (pszDest ? ConnectSocketByName(addrConnect, hSocket, pszDest, Params().GetDefaultPort(), nConnectTimeout,
                                    &proxyConnectionFailed)
              : ConnectSocket(addrConnect, hSocket, nConnectTimeout, &proxyConnectionFailed)) {
    if (!IsSelectableSocket(hSocket)) {
      LogPrintf("Cannot create connection: non-selectable socket created (fd >= FD_SETSIZE ?)\n");
      CloseSocket(hSocket);
      return nullptr;
    }

    addrman.Attempt(addrConnect);

    // Add node
    CNode* pnode = new CNode(hSocket, addrConnect, pszDest ? pszDest : "", false);
    pnode->AddRef();

    {
      LOCK(cs_vNodes);
      vNodes.push_back(pnode);
    }

    pnode->nTimeConnected = GetTime();
    return pnode;
  } else if (!proxyConnectionFailed) {
    // If connecting to the node failed, and failure is not caused by a problem connecting to
    // the proxy, mark this as an attempt.
    addrman.Attempt(addrConnect);
  }

  return nullptr;
}

void CNode::CloseSocketDisconnect() {
  fDisconnect = true;
  if (hSocket != INVALID_SOCKET) {
    LogPrint(TessaLog::NET, "disconnecting peer=%d\n", id);
    CloseSocket(hSocket);
  }

  // in case this fails, we'll empty the recv buffer when the CNode is deleted
  TRY_LOCK(cs_vRecvMsg, lockRecv);
  if (lockRecv) vRecvMsg.clear();
}

bool CNode::DisconnectOldProtocol(int nVersionRequired, const string& strLastCommand) {
  fDisconnect = false;
  if (nNodeVersion < nVersionRequired) {
    LogPrintf("%s : peer=%d using obsolete version %i; disconnecting\n", __func__, id, nNodeVersion);
    PushMessage("reject", strLastCommand, REJECT_OBSOLETE,
                strprintf("Version must be %d or greater", ActiveProtocol()));
    fDisconnect = true;
  }

  return fDisconnect;
}

void CNode::PushVersion() {
  int nBestHeight = 0;
  g_signals.GetHeight.fire(&nBestHeight);  // HACK ???.get_value_or(0);

  /// when NTP implemented, change to just nTime = GetAdjustedTime()
  int64_t nTime = (fInbound ? GetAdjustedTime() : GetTime());
  CAddress addrYou = (addr.IsRoutable() && !IsProxy(addr) ? addr : CAddress(CService("0.0.0.0", 0)));
  CAddress addrMe = GetLocalAddress(&addr);
  GetRandBytes((uint8_t*)&nLocalHostNonce, sizeof(nLocalHostNonce));
  if (fLogIPs)
    LogPrint(TessaLog::NET, "send version message: version %d, blocks=%d, us=%s, them=%s, peer=%d\n", PROTOCOL_VERSION,
             nBestHeight, addrMe.ToString(), addrYou.ToString(), id);
  else
    LogPrint(TessaLog::NET, "send version message: version %d, blocks=%d, us=%s, peer=%d\n", PROTOCOL_VERSION,
             nBestHeight, addrMe.ToString(), id);
  PushMessage("version", PROTOCOL_VERSION, nLocalServices, nTime, addrYou, addrMe, nLocalHostNonce,
              FormatSubVersion(CLIENT_NAME, CLIENT_VERSION, std::vector<string>()), nBestHeight, true);
}

banmap_t CNode::setBanned;
CCriticalSection CNode::cs_setBanned;
bool CNode::setBannedIsDirty;

void CNode::ClearBanned() {
  {
    LOCK(cs_setBanned);
    setBanned.clear();
    setBannedIsDirty = true;
  }
  DumpBanlist();  // store banlist to Disk
  uiInterface.BannedListChanged.fire();
}

bool CNode::IsBanned(CNetAddr ip) {
  bool fResult = false;
  {
    LOCK(cs_setBanned);
    for (const auto& it : setBanned) {
      CSubNet subNet = it.first;
      CBanEntry banEntry = it.second;

      if (subNet.Match(ip) && GetTime() < banEntry.nBanUntil) fResult = true;
    }
  }
  return fResult;
}

bool CNode::IsBanned(CSubNet subnet) {
  bool fResult = false;
  {
    LOCK(cs_setBanned);
    banmap_t::iterator i = setBanned.find(subnet);
    if (i != setBanned.end()) {
      CBanEntry banEntry = (*i).second;
      if (GetTime() < banEntry.nBanUntil) fResult = true;
    }
  }
  return fResult;
}

void CNode::Ban(const CNetAddr& addr, const BanReason& banReason, int64_t bantimeoffset, bool sinceUnixEpoch) {
  CSubNet subNet(addr);
  Ban(subNet, banReason, bantimeoffset, sinceUnixEpoch);
}

void CNode::Ban(const CSubNet& subNet, const BanReason& banReason, int64_t bantimeoffset, bool sinceUnixEpoch) {
  CBanEntry banEntry(GetTime());
  banEntry.banReason = banReason;
  if (bantimeoffset <= 0) {
    bantimeoffset = GetArg("-bantime", 60 * 60 * 24);  // Default 24-hour ban
    sinceUnixEpoch = false;
  }
  banEntry.nBanUntil = (sinceUnixEpoch ? 0 : GetTime()) + bantimeoffset;

  {
    LOCK(cs_setBanned);
    if (setBanned[subNet].nBanUntil < banEntry.nBanUntil) {
      setBanned[subNet] = banEntry;
      setBannedIsDirty = true;
    } else
      return;
  }
  uiInterface.BannedListChanged.fire();
  {
    LOCK(cs_vNodes);
    for (CNode* pnode : vNodes) {
      if (subNet.Match((CNetAddr)pnode->addr)) pnode->fDisconnect = true;
    }
  }
  if (banReason == BanReasonManuallyAdded) DumpBanlist();  // store banlist to disk immediately if user requested ban
}

bool CNode::Unban(const CNetAddr& addr) {
  CSubNet subNet(addr);
  return Unban(subNet);
}

bool CNode::Unban(const CSubNet& subNet) {
  {
    LOCK(cs_setBanned);
    if (!setBanned.erase(subNet)) return false;
    setBannedIsDirty = true;
  }
  uiInterface.BannedListChanged.fire();
  DumpBanlist();  // store banlist to disk immediately
  return true;
}

void CNode::GetBanned(banmap_t& banMap) {
  LOCK(cs_setBanned);
  banMap = setBanned;  // create a thread safe copy
}

void CNode::SetBanned(const banmap_t& banMap) {
  LOCK(cs_setBanned);
  setBanned = banMap;
  setBannedIsDirty = true;
}

void CNode::SweepBanned() {
  int64_t now = GetTime();

  bool notifyUI = false;
  {
    LOCK(cs_setBanned);
    banmap_t::iterator it = setBanned.begin();
    while (it != setBanned.end()) {
      CSubNet subNet = (*it).first;
      CBanEntry banEntry = (*it).second;
      if (now > banEntry.nBanUntil) {
        setBanned.erase(it++);
        setBannedIsDirty = true;
        notifyUI = true;
        LogPrint(TessaLog::NET, "%s: Removed banned node ip/subnet from banlist.dat: %s\n", __func__,
                 subNet.ToString());
      } else
        ++it;
    }
  }
  // update UI
  if (notifyUI) { uiInterface.BannedListChanged.fire(); }
}

bool CNode::BannedSetIsDirty() {
  LOCK(cs_setBanned);
  return setBannedIsDirty;
}

void CNode::SetBannedSetDirty(bool dirty) {
  LOCK(cs_setBanned);  // reuse setBanned lock for the isDirty flag
  setBannedIsDirty = dirty;
}

std::vector<CSubNet> CNode::vWhitelistedRange;
CCriticalSection CNode::cs_vWhitelistedRange;

bool CNode::IsWhitelistedRange(const CNetAddr& addr) {
  LOCK(cs_vWhitelistedRange);
  for (const CSubNet& subnet : vWhitelistedRange) {
    if (subnet.Match(addr)) return true;
  }
  return false;
}

void CNode::AddWhitelistedRange(const CSubNet& subnet) {
  LOCK(cs_vWhitelistedRange);
  vWhitelistedRange.push_back(subnet);
}

#undef X
#define X(name) stats.name = name
void CNode::copyStats(CNodeStats& stats) {
  stats.nodeid = this->GetId();
  X(nServices);
  X(nLastSend);
  X(nLastRecv);
  X(nTimeConnected);
  X(nTimeOffset);
  X(addrName);
  X(nNodeVersion);
  X(cleanSubVer);
  X(fInbound);
  X(nStartingHeight);
  X(nSendBytes);
  X(nRecvBytes);
  X(fWhitelisted);

  // It is common for nodes with good ping times to suddenly become lagged,
  // due to a new block arriving or other large transfer.
  // Merely reporting pingtime might fool the caller into thinking the node was still responsive,
  // since pingtime does not update until the ping is complete, which might take a while.
  // So, if a ping is taking an unusually long time in flight,
  // the caller can immediately detect that this is happening.
  int64_t nPingUsecWait = 0;
  if ((0 != nPingNonceSent) && (0 != nPingUsecStart)) { nPingUsecWait = GetTimeMicros() - nPingUsecStart; }

  // Raw ping time is in microseconds, but show it to user as whole seconds (Tessa users should be well used to small
  // numbers with many decimal places by now :)
  stats.dPingTime = (((double)nPingUsecTime) / 1e6);
  stats.dPingWait = (((double)nPingUsecWait) / 1e6);

  // Leave string empty if addrLocal invalid (not filled in yet)
  stats.addrLocal = addrLocal.IsValid() ? addrLocal.ToString() : "";
}
#undef X

// requires LOCK(cs_vRecvMsg)
bool CNode::ReceiveMsgBytes(const char* pch, uint32_t nBytes) {
  while (nBytes > 0) {
    // get current incomplete message, or create a new one
    if (vRecvMsg.empty() || vRecvMsg.back().complete()) vRecvMsg.push_back(CNetMessage(SER_NETWORK, nRecvVersion));

    CNetMessage& msg = vRecvMsg.back();

    // absorb network data
    int handled;
    if (!msg.in_data)
      handled = msg.readHeader(pch, nBytes);
    else
      handled = msg.readData(pch, nBytes);

    if (handled < 0) return false;

    if (msg.in_data && msg.hdr.nMessageSize > MAX_PROTOCOL_MESSAGE_LENGTH) {
      LogPrint(TessaLog::NET, "Oversized message from peer=%i, disconnecting", GetId());
      return false;
    }

    pch += handled;
    nBytes -= handled;

    if (msg.complete()) {
      msg.nTime = GetTimeMicros();
      messageHandlerCondition.notify_one();
    }
  }

  return true;
}

int CNetMessage::readHeader(const char* pch, uint32_t nBytes) {
  // copy data to temporary parsing buffer
  uint32_t nRemaining = 24 - nHdrPos;
  uint32_t nCopy = std::min(nRemaining, nBytes);

  memcpy(&hdrbuf[nHdrPos], pch, nCopy);
  nHdrPos += nCopy;

  // if header incomplete, exit
  if (nHdrPos < 24) return nCopy;

  // deserialize to CMessageHeader
  try {
    hdrbuf >> hdr;
  } catch (const std::exception&) { return -1; }

  // reject messages larger than MAX_SIZE
  if (hdr.nMessageSize > MAX_SIZE) return -1;

  // switch state to reading message data
  in_data = true;

  return nCopy;
}

int CNetMessage::readData(const char* pch, uint32_t nBytes) {
  uint32_t nRemaining = hdr.nMessageSize - nDataPos;
  uint32_t nCopy = std::min(nRemaining, nBytes);

  if (vRecv.size() < nDataPos + nCopy) {
    // Allocate up to 256 KiB ahead, but never more than the total message size.
    vRecv.resize(std::min(hdr.nMessageSize, nDataPos + nCopy + 256 * 1024));
  }

  memcpy(&vRecv[nDataPos], pch, nCopy);
  nDataPos += nCopy;

  return nCopy;
}

// requires LOCK(cs_vSend)
void SocketSendData(CNode* pnode) {
  std::deque<CSerializeData>::iterator it = pnode->vSendMsg.begin();

  while (it != pnode->vSendMsg.end()) {
    const CSerializeData& data = *it;
    assert(data.size() > pnode->nSendOffset);
    int nBytes =
        send(pnode->hSocket, &data[pnode->nSendOffset], data.size() - pnode->nSendOffset, MSG_NOSIGNAL | MSG_DONTWAIT);
    if (nBytes > 0) {
      pnode->nLastSend = GetTime();
      pnode->nSendBytes += nBytes;
      pnode->nSendOffset += nBytes;
      pnode->RecordBytesSent(nBytes);
      if (pnode->nSendOffset == data.size()) {
        pnode->nSendOffset = 0;
        pnode->nSendSize -= data.size();
        it++;
      } else {
        // could not send full message; stop sending more
        break;
      }
    } else {
      if (nBytes < 0) {
        // error
        int nErr = WSAGetLastError();
        if (nErr != WSAEWOULDBLOCK && nErr != WSAEMSGSIZE && nErr != WSAEINTR && nErr != WSAEINPROGRESS) {
          LogPrintf("socket send error %s\n", NetworkErrorString(nErr));
          pnode->CloseSocketDisconnect();
        }
      }
      // couldn't send anything at all
      break;
    }
  }

  if (it == pnode->vSendMsg.end()) {
    assert(pnode->nSendOffset == 0);
    assert(pnode->nSendSize == 0);
  }
  pnode->vSendMsg.erase(pnode->vSendMsg.begin(), it);
}

static list<CNode*> vNodesDisconnected;

void ThreadSocketHandler() {
  uint32_t nPrevNodeCount = 0;
  while (!net_interrupted) {
    //
    // Disconnect nodes
    //
    {
      LOCK(cs_vNodes);
      // Disconnect unused nodes
      vector<CNode*> vNodesCopy = vNodes;
      for (CNode* pnode : vNodesCopy) {
        if (pnode->fDisconnect ||
            (pnode->GetRefCount() <= 0 && pnode->vRecvMsg.empty() && pnode->nSendSize == 0 && pnode->ssSend.empty())) {
          // remove from vNodes
          vNodes.erase(remove(vNodes.begin(), vNodes.end(), pnode), vNodes.end());

          // release outbound grant (if any)
          pnode->grantOutbound.Release();

          // close socket and cleanup
          pnode->CloseSocketDisconnect();

          // hold in disconnected pool until all refs are released
          if (pnode->fNetworkNode || pnode->fInbound) pnode->Release();
          vNodesDisconnected.push_back(pnode);
        }
      }
    }
    {
      // Delete disconnected nodes
      list<CNode*> vNodesDisconnectedCopy = vNodesDisconnected;
      for (CNode* pnode : vNodesDisconnectedCopy) {
        // wait until threads are done using it
        if (pnode->GetRefCount() <= 0) {
          bool fDelete = false;
          {
            TRY_LOCK(pnode->cs_vSend, lockSend);
            if (lockSend) {
              TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
              if (lockRecv) {
                TRY_LOCK(pnode->cs_inventory, lockInv);
                if (lockInv) fDelete = true;
              }
            }
          }
          if (fDelete) {
            vNodesDisconnected.remove(pnode);
            delete pnode;
          }
        }
      }
    }
    size_t vNodesSize;
    {
      LOCK(cs_vNodes);
      vNodesSize = vNodes.size();
    }
    if (vNodesSize != nPrevNodeCount) {
      nPrevNodeCount = vNodesSize;
      uiInterface.NotifyNumConnectionsChanged.fire(nPrevNodeCount);
    }

    //
    // Find which sockets have data to receive
    //
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 50000;  // frequency to poll pnode->vSend

    fd_set fdsetRecv;
    fd_set fdsetSend;
    fd_set fdsetError;
    FD_ZERO(&fdsetRecv);
    FD_ZERO(&fdsetSend);
    FD_ZERO(&fdsetError);
    SOCKET hSocketMax = 0;
    bool have_fds = false;

    for (const ListenSocket& hListenSocket : vhListenSocket) {
      FD_SET(hListenSocket.socket, &fdsetRecv);
      hSocketMax = max(hSocketMax, hListenSocket.socket);
      have_fds = true;
    }

    {
      LOCK(cs_vNodes);
      for (CNode* pnode : vNodes) {
        if (pnode->hSocket == INVALID_SOCKET) continue;
        FD_SET(pnode->hSocket, &fdsetError);
        hSocketMax = max(hSocketMax, pnode->hSocket);
        have_fds = true;

        // Implement the following logic:
        // * If there is data to send, select() for sending data. As this only
        //   happens when optimistic write failed, we choose to first drain the
        //   write buffer in this case before receiving more. This avoids
        //   needlessly queueing received data, if the remote peer is not themselves
        //   receiving data. This means properly utilizing TCP flow control signalling.
        // * Otherwise, if there is no (complete) message in the receive buffer,
        //   or there is space left in the buffer, select() for receiving data.
        // * (if neither of the above applies, there is certainly one message
        //   in the receiver buffer ready to be processed).
        // Together, that means that at least one of the following is always possible,
        // so we don't deadlock:
        // * We send some data.
        // * We wait for data to be received (and disconnect after timeout).
        // * We process a message in the buffer (message handler thread).
        {
          TRY_LOCK(pnode->cs_vSend, lockSend);
          if (lockSend && !pnode->vSendMsg.empty()) {
            FD_SET(pnode->hSocket, &fdsetSend);
            continue;
          }
        }
        {
          TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
          if (lockRecv && (pnode->vRecvMsg.empty() || !pnode->vRecvMsg.front().complete() ||
                           pnode->GetTotalRecvSize() <= ReceiveFloodSize()))
            FD_SET(pnode->hSocket, &fdsetRecv);
        }
      }
    }

    int nSelect = select(have_fds ? hSocketMax + 1 : 0, &fdsetRecv, &fdsetSend, &fdsetError, &timeout);
    if (net_interrupted) break;

    if (nSelect == SOCKET_ERROR) {
      if (have_fds) {
        int nErr = WSAGetLastError();
        LogPrintf("socket select error %s\n", NetworkErrorString(nErr));
        for (uint32_t i = 0; i <= hSocketMax; i++) FD_SET(i, &fdsetRecv);
      }
      FD_ZERO(&fdsetSend);
      FD_ZERO(&fdsetError);
      InterruptibleSleep(timeout.tv_usec / 1000);
    }

    //
    // Accept new connections
    //
    for (const ListenSocket& hListenSocket : vhListenSocket) {
      if (hListenSocket.socket != INVALID_SOCKET && FD_ISSET(hListenSocket.socket, &fdsetRecv)) {
        struct sockaddr_storage sockaddr;
        socklen_t len = sizeof(sockaddr);
        SOCKET hSocket = accept(hListenSocket.socket, (struct sockaddr*)&sockaddr, &len);
        CAddress addr;
        int nInbound = 0;

        if (hSocket != INVALID_SOCKET)
          if (!addr.SetSockAddr((const struct sockaddr*)&sockaddr)) LogPrintf("Warning: Unknown socket family\n");

        bool whitelisted = hListenSocket.whitelisted || CNode::IsWhitelistedRange(addr);
        {
          LOCK(cs_vNodes);
          for (CNode* pnode : vNodes)
            if (pnode->fInbound) nInbound++;
        }

        if (hSocket == INVALID_SOCKET) {
          int nErr = WSAGetLastError();
          if (nErr != WSAEWOULDBLOCK) LogPrintf("socket error accept failed: %s\n", NetworkErrorString(nErr));
        } else if (!IsSelectableSocket(hSocket)) {
          LogPrintf("connection from %s dropped: non-selectable socket\n", addr.ToString());
          CloseSocket(hSocket);
        } else if (nInbound >= nMaxConnections - MAX_OUTBOUND_CONNECTIONS) {
          LogPrint(TessaLog::NET, "connection from %s dropped (full)\n", addr.ToString());
          CloseSocket(hSocket);
        } else if (CNode::IsBanned(addr) && !whitelisted) {
          LogPrint(TessaLog::NET, "connection from %s dropped (banned)\n", addr.ToString());
          CloseSocket(hSocket);
        } else {
          CNode* pnode = new CNode(hSocket, addr, "", true);
          pnode->AddRef();
          pnode->fWhitelisted = whitelisted;

          {
            LOCK(cs_vNodes);
            vNodes.push_back(pnode);
          }
        }
      }
    }

    //
    // Service each socket
    //
    vector<CNode*> vNodesCopy;
    {
      LOCK(cs_vNodes);
      vNodesCopy = vNodes;
      for (CNode* pnode : vNodesCopy) pnode->AddRef();
    }
    for (CNode* pnode : vNodesCopy) {
      if (net_interrupted) break;

      //
      // Receive
      //
      if (pnode->hSocket == INVALID_SOCKET) continue;
      if (FD_ISSET(pnode->hSocket, &fdsetRecv) || FD_ISSET(pnode->hSocket, &fdsetError)) {
        TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
        if (lockRecv) {
          {
            // typical socket buffer is 8K-64K
            char pchBuf[0x10000];
            int nBytes = recv(pnode->hSocket, pchBuf, sizeof(pchBuf), MSG_DONTWAIT);
            if (nBytes > 0) {
              if (!pnode->ReceiveMsgBytes(pchBuf, nBytes)) pnode->CloseSocketDisconnect();
              pnode->nLastRecv = GetTime();
              pnode->nRecvBytes += nBytes;
              pnode->RecordBytesRecv(nBytes);
            } else if (nBytes == 0) {
              // socket closed gracefully
              if (!pnode->fDisconnect) LogPrint(TessaLog::NET, "socket closed\n");
              pnode->CloseSocketDisconnect();
            } else if (nBytes < 0) {
              // error
              int nErr = WSAGetLastError();
              if (nErr != WSAEWOULDBLOCK && nErr != WSAEMSGSIZE && nErr != WSAEINTR && nErr != WSAEINPROGRESS) {
                if (!pnode->fDisconnect) LogPrintf("socket recv error %s\n", NetworkErrorString(nErr));
                pnode->CloseSocketDisconnect();
              }
            }
          }
        }
      }

      //
      // Send
      //
      if (pnode->hSocket == INVALID_SOCKET) continue;
      if (FD_ISSET(pnode->hSocket, &fdsetSend)) {
        TRY_LOCK(pnode->cs_vSend, lockSend);
        if (lockSend) SocketSendData(pnode);
      }

      //
      // Inactivity checking
      //
      int64_t nTime = GetTime();
      if (nTime - pnode->nTimeConnected > 60) {
        if (pnode->nLastRecv == 0 || pnode->nLastSend == 0) {
          LogPrint(TessaLog::NET, "socket no message in first 60 seconds, %d %d from %d\n", pnode->nLastRecv != 0,
                   pnode->nLastSend != 0, pnode->id);
          pnode->fDisconnect = true;
        } else if (nTime - pnode->nLastSend > TIMEOUT_INTERVAL) {
          LogPrint(TessaLog::NET, "socket sending timeout: %is\n", nTime - pnode->nLastSend);
          pnode->fDisconnect = true;
        } else if (nTime - pnode->nLastRecv > TIMEOUT_INTERVAL) {
          LogPrint(TessaLog::NET, "socket receive timeout: %is\n", nTime - pnode->nLastRecv);
          pnode->fDisconnect = true;
        } else if (pnode->nPingNonceSent && pnode->nPingUsecStart + TIMEOUT_INTERVAL * 1000000 < GetTimeMicros()) {
          LogPrint(TessaLog::NET, "ping timeout: %fs\n", 0.000001 * (GetTimeMicros() - pnode->nPingUsecStart));
          pnode->fDisconnect = true;
        }
      }
    }
    {
      LOCK(cs_vNodes);
      for (CNode* pnode : vNodesCopy) pnode->Release();
    }
  }
}

#ifdef USE_UPNP

static std::condition_variable upnp_cond;
static std::mutex cs_upnp;
static bool upnp_interrupted = false;
static std::thread* upnp_thread = NULL;

void ThreadMapPort() {
  std::string port = strprintf("%u", GetListenPort());
  const char* multicastif = 0;
  const char* minissdpdpath = 0;
  struct UPNPDev* devlist = 0;
  char lanaddr[64];

#ifndef UPNPDISCOVER_SUCCESS
  /* miniupnpc 1.5 */
  devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0);
#elif MINIUPNPC_API_VERSION < 14
  /* miniupnpc 1.6 */
  int error = 0;
  devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0, 0, &error);
#else
  /* miniupnpc 1.9.20150730 */
  int error = 0;
  devlist = upnpDiscover(2000, multicastif, minissdpdpath, 0, 0, 2, &error);
#endif

  struct UPNPUrls urls;
  struct IGDdatas data;
  int r;

  r = UPNP_GetValidIGD(devlist, &urls, &data, lanaddr, sizeof(lanaddr));
  if (r == 1) {
    if (fDiscover) {
      char externalIPAddress[40];
      r = UPNP_GetExternalIPAddress(urls.controlURL, data.first.servicetype, externalIPAddress);
      if (r != UPNPCOMMAND_SUCCESS)
        LogPrint(TessaLog::NET, "UPnP: GetExternalIPAddress() returned %d\n", r);
      else {
        if (externalIPAddress[0]) {
          LogPrint(TessaLog::NET, "UPnP: ExternalIPAddress = %s\n", externalIPAddress);
          AddLocal(CNetAddr(externalIPAddress), LOCAL_UPNP);
        } else
          LogPrint(TessaLog::NET, "UPnP: GetExternalIPAddress failed.\n");
      }
    }

    string strDesc = "Tessa " + FormatFullVersion();

    bool interrupted = false;
    while (!interrupted) {
#ifndef UPNPDISCOVER_SUCCESS
      /* miniupnpc 1.5 */
      r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, port.c_str(), port.c_str(), lanaddr,
                              strDesc.c_str(), "TCP", 0);
#else
      /* miniupnpc 1.6 */
      r = UPNP_AddPortMapping(urls.controlURL, data.first.servicetype, port.c_str(), port.c_str(), lanaddr,
                              strDesc.c_str(), "TCP", 0, "0");
#endif

      if (r != UPNPCOMMAND_SUCCESS)
        LogPrint(TessaLog::NET, "AddPortMapping(%s, %s, %s) failed with code %d (%s)\n", port, port, lanaddr, r,
                 strupnperror(r));
      else
        LogPrint(TessaLog::NET, "UPnP Port Mapping successful.\n");
      ;

      std::unique_lock<std::mutex> lock(cs_upnp);
      ////MilliSleep(20 * 60 * 1000);  // Refresh every 20 minutes
      interrupted = upnp_cond.wait_for(lock, std::chrono::minutes(20), [] { return upnp_interrupted; });
    }
    if (interrupted) {
      r = UPNP_DeletePortMapping(urls.controlURL, data.first.servicetype, port.c_str(), "TCP", 0);
      LogPrint(TessaLog::NET, "UPNP_DeletePortMapping() returned: %d\n", r);
      freeUPNPDevlist(devlist);
      devlist = 0;
      FreeUPNPUrls(&urls);
    }
  } else {
    LogPrint(TessaLog::NET, "No valid UPnP IGDs found\n");
    freeUPNPDevlist(devlist);
    devlist = 0;
    if (r != 0) FreeUPNPUrls(&urls);
  }
}

void InterruptMapPort() {
  LogPrint(TessaLog::NET, "Interrupting UPnP\n");
  {
    std::lock_guard<std::mutex> lock(cs_upnp);
    upnp_interrupted = true;
  }
  upnp_cond.notify_all();
}
void StopMapPort() {
  if (upnp_thread) {
    LogPrint(TessaLog::NET, "Stopping UPnP\n");
    upnp_thread->join();
    delete upnp_thread;
    upnp_thread = NULL;
  }
  std::lock_guard<std::mutex> lock(cs_upnp);
  upnp_interrupted = false;
}

void MapPort(bool fUseUPnP) {
  static std::thread upnp_thread;
  if (fUseUPnP) {
    InterruptMapPort();
    StopMapPort();
    upnp_thread = std::thread(([&] { TraceThread<void (*)()>("upnp", &ThreadMapPort); }));
  } else {
    InterruptMapPort();
    StopMapPort();
  }
}

#else
void MapPort(bool) {
  // Intentionally left blank.
}
void StopMapPort() {
  // Intentionally left blank.
}
void InterruptMapPort() {
  // Intentionally left blank.
}

#endif

void ThreadDNSAddressSeed() {
  // goal: only query DNS seeds if address need is acute
  if ((addrman.size() > 0) && (!GetBoolArg("-forcednsseed", false))) {
    {
      std::unique_lock<std::mutex> lock(cs_net_interrupt);
      if (net_interrupt_cond.wait_for(lock, std::chrono::seconds(11), []() -> bool { return net_interrupted; })) return;
    }

    LOCK(cs_vNodes);
    if (vNodes.size() >= 2) {
      LogPrintf("P2P peers available. Skipped DNS seeding.\n");
      return;
    }
  }

  const vector<CDNSSeedData>& vSeeds = Params().DNSSeeds();
  int found = 0;

  if (vSeeds.size() > 0) {
    LogPrintf("Loading addresses from DNS seeds (could take a while)\n");

    for (const CDNSSeedData& seed : vSeeds) {
      if (net_interrupted) return;
      if (HaveNameProxy()) {
        AddOneShot(seed.host);
      } else {
        vector<CNetAddr> vIPs;
        vector<CAddress> vAdd;
        if (LookupHost(seed.host.c_str(), vIPs)) {
          for (CNetAddr& ip : vIPs) {
            int nOneDay = 24 * 3600;
            CAddress addr = CAddress(CService(ip, Params().GetDefaultPort()));
            addr.nTime = GetTime() - 3 * nOneDay - GetRand(4 * nOneDay);  // use a random age between 3 and 7 days old
            vAdd.push_back(addr);
            found++;
          }
        }
        addrman.Add(vAdd, CNetAddr(seed.name, true));
      }
    }

    LogPrintf("%d addresses found from DNS seeds\n", found);
  } else {
    LogPrintf("No DNS seeds setup\n");
  }
}

void DumpAddresses() {
  int64_t nStart = GetTimeMillis();

  CAddrDB adb;
  adb.Write(addrman);

  LogPrint(TessaLog::NET, "Flushed %d addresses to peers.dat  %dms\n", addrman.size(), GetTimeMillis() - nStart);
}

void DumpData() {
  DumpAddresses();
  DumpBanlist();
}

void static ProcessOneShot() {
  string strDest;
  {
    LOCK(cs_vOneShots);
    if (vOneShots.empty()) return;
    strDest = vOneShots.front();
    vOneShots.pop_front();
  }
  CAddress addr;
  CSemaphoreGrant grant(*semOutbound, true);
  if (grant) {
    if (!OpenNetworkConnection(addr, &grant, strDest.c_str(), true)) AddOneShot(strDest);
  }
}

void ThreadOpenConnections() {
  // Connect to specific addresses
  if (gArgs.IsArgSet("-connect")) {
    for (int64_t nLoop = 0;; nLoop++) {
      ProcessOneShot();
      for (auto strAddr : gArgs.GetArgs("-connect")) {
        CAddress addr;
        OpenNetworkConnection(addr, nullptr, strAddr.c_str());
        for (int i = 0; i < 10 && i < nLoop; i++) { InterruptibleSleep(500); }
      }
      InterruptibleSleep(500);
    }
  }

  // Initiate network connections
  int64_t nStart = GetTime();
  while (true) {
    ProcessOneShot();
    InterruptibleSleep(500);

    CSemaphoreGrant grant(*semOutbound);
    if (net_interrupted) { break; }
    // Add seed nodes if DNS seeds are all down (an infrastructure attack?).
    if (addrman.size() == 0 && (GetTime() - nStart > 60)) {
      static bool done = false;
      if (!done) {
        LogPrintf("Adding fixed seed nodes as DNS doesn't seem to be available.\n");
        addrman.Add(Params().FixedSeeds(), CNetAddr("127.0.0.1"));
        done = true;
      }
    }

    //
    // Choose an address to connect to based on most recently seen
    //
    CAddress addrConnect;

    // Only connect out to one peer per network group (/16 for IPv4).
    // Do this here so we don't have to critsect vNodes inside mapAddresses critsect.
    int nOutbound = 0;
    set<vector<uint8_t> > setConnected;
    {
      LOCK(cs_vNodes);
      for (CNode* pnode : vNodes) {
        if (!pnode->fInbound) {
          setConnected.insert(pnode->addr.GetGroup());
          nOutbound++;
        }
      }
    }

    int64_t nANow = GetAdjustedTime();

    int nTries = 0;
    while (true) {
      CAddress addr = addrman.Select();

      // if we selected an invalid address, restart
      if (!addr.IsValid() || setConnected.count(addr.GetGroup()) || IsLocal(addr)) break;

      // If we didn't find an appropriate destination after trying 100 addresses fetched from addrman,
      // stop this loop, and let the outer loop run again (which sleeps, adds seed nodes, recalculates
      // already-connected network ranges, ...) before trying new addrman addresses.
      nTries++;
      if (nTries > 100) break;

      if (IsLimited(addr)) continue;

      // only consider very recently tried nodes after 30 failed attempts
      if (nANow - addr.nLastTry < 600 && nTries < 30) continue;

      // do not allow non-default ports, unless after 50 invalid addresses selected already
      if (addr.GetPort() != Params().GetDefaultPort() && nTries < 50) continue;

      addrConnect = addr;
      break;
    }

    if (addrConnect.IsValid()) OpenNetworkConnection(addrConnect, &grant);
  }
}

void ThreadOpenAddedConnections() {
  {
    LOCK(cs_vAddedNodes);
    vAddedNodes = gArgs.GetArgs("-addnode");
  }

  if (HaveNameProxy()) {
    while (true) {
      list<string> lAddresses(0);
      {
        LOCK(cs_vAddedNodes);
        for (string& strAddNode : vAddedNodes) lAddresses.push_back(strAddNode);
      }
      for (string& strAddNode : lAddresses) {
        CAddress addr;
        CSemaphoreGrant grant(*semOutbound);
        OpenNetworkConnection(addr, &grant, strAddNode.c_str());
        InterruptibleSleep(500);
      }
      InterruptibleSleep(120000);
      if (net_interrupted) break;
    }
  }

  for (uint32_t i = 0; true; i++) {
    list<string> lAddresses(0);
    {
      LOCK(cs_vAddedNodes);
      for (string& strAddNode : vAddedNodes) lAddresses.push_back(strAddNode);
    }

    list<vector<CService> > lservAddressesToAdd(0);
    for (string& strAddNode : lAddresses) {
      vector<CService> vservNode(0);
      if (Lookup(strAddNode.c_str(), vservNode, Params().GetDefaultPort(), fNameLookup, 0)) {
        lservAddressesToAdd.push_back(vservNode);
        {
          LOCK(cs_setservAddNodeAddresses);
          for (CService& serv : vservNode) setservAddNodeAddresses.insert(serv);
        }
      }
    }
    // Attempt to connect to each IP for each addnode entry until at least one is successful per addnode entry
    // (keeping in mind that addnode entries can have many IPs if fNameLookup)
    {
      LOCK(cs_vNodes);
      for (CNode* pnode : vNodes)
        for (list<vector<CService> >::iterator it = lservAddressesToAdd.begin(); it != lservAddressesToAdd.end(); it++)
          for (CService& addrNode : *(it))
            if (pnode->addr == addrNode) {
              it = lservAddressesToAdd.erase(it);
              it--;
              break;
            }
    }
    for (vector<CService>& vserv : lservAddressesToAdd) {
      CSemaphoreGrant grant(*semOutbound);
      OpenNetworkConnection(CAddress(vserv[i % vserv.size()]), &grant);
      InterruptibleSleep(500);
    }
    InterruptibleSleep(120000);  // Retry every 2 minutes
    if (net_interrupted) break;
  }
}

// if successful, this moves the passed grant to the constructed node
bool OpenNetworkConnection(const CAddress& addrConnect, CSemaphoreGrant* grantOutbound, const char* pszDest,
                           bool fOneShot) {
  //
  // Initiate outbound network connection
  //
  interruption_point(net_interrupted);
  if (!pszDest) {
    if (IsLocal(addrConnect) || FindNode((CNetAddr)addrConnect) || CNode::IsBanned(addrConnect) ||
        FindNode(addrConnect.ToStringIPPort()))
      return false;
  } else if (FindNode(pszDest))
    return false;

  CNode* pnode = ConnectNode(addrConnect, pszDest);
  interruption_point(net_interrupted);

  if (!pnode) return false;
  if (grantOutbound) grantOutbound->MoveTo(pnode->grantOutbound);
  pnode->fNetworkNode = true;
  if (fOneShot) pnode->fOneShot = true;

  return true;
}

void ThreadMessageHandler() {
  std::mutex condition_mutex;
  std::unique_lock<std::mutex> lock(condition_mutex);

  SetThreadPriority(THREAD_PRIORITY_BELOW_NORMAL);
  while (!net_interrupted) {
    vector<CNode*> vNodesCopy;
    {
      LOCK(cs_vNodes);
      vNodesCopy = vNodes;
      for (CNode* pnode : vNodesCopy) { pnode->AddRef(); }
    }

    // Poll the connected nodes for messages
    CNode* pnodeTrickle = nullptr;
    if (!vNodesCopy.empty()) pnodeTrickle = vNodesCopy[GetRand(vNodesCopy.size())];

    bool fSleep = true;

    for (CNode* pnode : vNodesCopy) {
      if (pnode->fDisconnect) continue;

      // Receive messages
      {
        TRY_LOCK(pnode->cs_vRecvMsg, lockRecv);
        if (lockRecv) {
          bool ok;
          g_signals.ProcessMessagesSignal.fire(pnode, &ok);
          if (!ok) pnode->CloseSocketDisconnect();

          if (pnode->nSendSize < SendBufferSize()) {
            if (!pnode->vRecvGetData.empty() || (!pnode->vRecvMsg.empty() && pnode->vRecvMsg[0].complete())) {
              fSleep = false;
            }
          }
        }
      }
      if (net_interrupted) break;

      // Send messages
      {
        TRY_LOCK(pnode->cs_vSend, lockSend);
        if (lockSend) g_signals.SendMessages.fire(pnode, pnode == pnodeTrickle || pnode->fWhitelisted);
      }
      if (net_interrupted) break;
    }

    {
      LOCK(cs_vNodes);
      for (CNode* pnode : vNodesCopy) pnode->Release();
    }

    if (fSleep)
      messageHandlerCondition.wait_for(lock, std::chrono::milliseconds(100), []() -> bool { return net_interrupted; });
  }
}

// ppcoin: stake minter thread
void static ThreadStakeMinter() {
  LogPrintf("ThreadStakeMinter started\n");
  try {
    BitcoinMiner(pwalletMain, true);
    interruption_point(net_interrupted);
  } catch (thread_interrupted& e) {
    LogPrintf("ThreadStakeMinter() interrupted\n");  ///
                                                     //
  } catch (...) { LogPrintf("ThreadStakeMinter() error \n"); }
  LogPrintf("ThreadStakeMinter exiting,\n");
}

bool BindListenPort(const CService& addrBind, string& strError, bool fWhitelisted) {
  strError = "";
  int nOne = 1;

  // Create socket for listening for incoming connections
  struct sockaddr_storage sockaddr;
  socklen_t len = sizeof(sockaddr);
  if (!addrBind.GetSockAddr((struct sockaddr*)&sockaddr, &len)) {
    strError = strprintf("Error: Bind address family for %s not supported", addrBind.ToString());
    LogPrintf("%s\n", strError);
    return false;
  }

  SOCKET hListenSocket = socket(((struct sockaddr*)&sockaddr)->sa_family, SOCK_STREAM, IPPROTO_TCP);
  if (hListenSocket == INVALID_SOCKET) {
    strError = strprintf("Error: Couldn't open socket for incoming connections (socket returned error %s)",
                         NetworkErrorString(WSAGetLastError()));
    LogPrintf("%s\n", strError);
    return false;
  }
  if (!IsSelectableSocket(hListenSocket)) {
    strError = "Error: Couldn't create a listenable socket for incoming connections";
    LogPrintf("%s\n", strError);
    return false;
  }

#ifndef WIN32
#ifdef SO_NOSIGPIPE
  // Different way of disabling SIGPIPE on BSD
  setsockopt(hListenSocket, SOL_SOCKET, SO_NOSIGPIPE, (void*)&nOne, sizeof(int));
#endif
  // Allow binding if the port is still in TIME_WAIT state after
  // the program was closed and restarted. Not an issue on windows!
  setsockopt(hListenSocket, SOL_SOCKET, SO_REUSEADDR, (void*)&nOne, sizeof(int));
#endif

  // Set to non-blocking, incoming connections will also inherit this
  if (!SetSocketNonBlocking(hListenSocket, true)) {
    strError = strprintf("BindListenPort: Setting listening socket to non-blocking failed, error %s\n",
                         NetworkErrorString(WSAGetLastError()));
    LogPrintf("%s\n", strError);
    return false;
  }

  // some systems don't have IPV6_V6ONLY but are always v6only; others do have the option
  // and enable it by default or not. Try to enable it, if possible.
  if (addrBind.IsIPv6()) {
#ifdef IPV6_V6ONLY
#ifdef WIN32
    setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&nOne, sizeof(int));
#else
    setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_V6ONLY, (void*)&nOne, sizeof(int));
#endif
#endif
#ifdef WIN32
    int nProtLevel = PROTECTION_LEVEL_UNRESTRICTED;
    setsockopt(hListenSocket, IPPROTO_IPV6, IPV6_PROTECTION_LEVEL, (const char*)&nProtLevel, sizeof(int));
#endif
  }

  if (::bind(hListenSocket, (struct sockaddr*)&sockaddr, len) == SOCKET_ERROR) {
    int nErr = WSAGetLastError();
    if (nErr == WSAEADDRINUSE)
      strError = strprintf(_("Unable to bind to %s on this computer. Tessa Core is probably already running."),
                           addrBind.ToString());
    else
      strError = strprintf(_("Unable to bind to %s on this computer (bind returned error %s)"), addrBind.ToString(),
                           NetworkErrorString(nErr));
    LogPrintf("%s\n", strError);
    CloseSocket(hListenSocket);
    return false;
  }
  LogPrint(TessaLog::NET, "Bound to %s\n", addrBind.ToString());

  // Listen for incoming connections
  if (listen(hListenSocket, SOMAXCONN) == SOCKET_ERROR) {
    strError = strprintf(_("Error: Listening for incoming connections failed (listen returned error %s)"),
                         NetworkErrorString(WSAGetLastError()));
    LogPrintf("%s\n", strError);
    CloseSocket(hListenSocket);
    return false;
  }

  vhListenSocket.push_back(ListenSocket(hListenSocket, fWhitelisted));

  if (addrBind.IsRoutable() && fDiscover && !fWhitelisted) AddLocal(addrBind, LOCAL_BIND);

  return true;
}

void static Discover() {
  if (!fDiscover) return;

#ifdef WIN32
  // Get local host IP
  char pszHostName[256] = "";
  if (gethostname(pszHostName, sizeof(pszHostName)) != SOCKET_ERROR) {
    vector<CNetAddr> vaddr;
    if (LookupHost(pszHostName, vaddr)) {
      for (const CNetAddr& addr : vaddr) {
        if (AddLocal(addr, LOCAL_IF)) LogPrint(TessaLog::NET, "%s: %s - %s\n", __func__, pszHostName, addr.ToString());
      }
    }
  }
#else
  // Get local host ip
  struct ifaddrs* myaddrs;
  if (getifaddrs(&myaddrs) == 0) {
    for (struct ifaddrs* ifa = myaddrs; ifa != nullptr; ifa = ifa->ifa_next) {
      if (ifa->ifa_addr == nullptr) continue;
      if ((ifa->ifa_flags & IFF_UP) == 0) continue;
      if (strcmp(ifa->ifa_name, "lo") == 0) continue;
      if (strcmp(ifa->ifa_name, "lo0") == 0) continue;
      if (ifa->ifa_addr->sa_family == AF_INET) {
        struct sockaddr_in* s4 = (struct sockaddr_in*)(ifa->ifa_addr);
        CNetAddr addr(s4->sin_addr);
        if (AddLocal(addr, LOCAL_IF))
          LogPrint(TessaLog::NET, "%s: IPv4 %s: %s\n", __func__, ifa->ifa_name, addr.ToString());
      } else if (ifa->ifa_addr->sa_family == AF_INET6) {
        struct sockaddr_in6* s6 = (struct sockaddr_in6*)(ifa->ifa_addr);
        CNetAddr addr(s6->sin6_addr);
        if (AddLocal(addr, LOCAL_IF))
          LogPrint(TessaLog::NET, "%s: IPv6 %s: %s\n", __func__, ifa->ifa_name, addr.ToString());
      }
    }
    freeifaddrs(myaddrs);
  }
#endif
}

void StartNode(CScheduler& scheduler) {
  net_interrupted = false;
  uiInterface.InitMessage.fire(_("Loading addresses..."));
  // Load addresses for peers.dat
  int64_t nStart = GetTimeMillis();
  {
    CAddrDB adb;
    if (!adb.Read(addrman)) LogPrintf("Invalid or missing peers.dat; recreating\n");
  }

  // try to read stored banlist
  CBanDB bandb;
  banmap_t banmap;
  if (!bandb.Read(banmap)) LogPrintf("Invalid or missing banlist.dat; recreating\n");

  CNode::SetBanned(banmap);         // thread save setter
  CNode::SetBannedSetDirty(false);  // no need to write down just read or nonexistent data
  CNode::SweepBanned();             // sweap out unused entries

  // Initialize random numbers. Even when rand() is only usable for trivial use-cases most nodes should have a different
  // seed after all the file-IO done at this point. Should be good enough even when nodes are started via scripts.
  srand(time(nullptr));

  LogPrint(TessaLog::NET, "Loaded %i addresses from peers.dat  %dms\n", addrman.size(), GetTimeMillis() - nStart);
  fAddressesInitialized = true;

  if (semOutbound == nullptr) {
    // initialize semaphore
    int nMaxOutbound = min(MAX_OUTBOUND_CONNECTIONS, nMaxConnections);
    semOutbound = new CSemaphore(nMaxOutbound);
  }

  if (pnodeLocalHost == nullptr)
    pnodeLocalHost = new CNode(INVALID_SOCKET, CAddress(CService("127.0.0.1", 0), nLocalServices));

  Discover();

  //
  // Start threads
  //

  if (!GetBoolArg("-dnsseed", true))
    LogPrintf("DNS seeding disabled\n");
  else
    dns_address_seed_thread = std::thread(std::bind(&TraceThread<void (*)()>, "dnsseed", &ThreadDNSAddressSeed));

  // Map ports with UPnP
  MapPort(GetBoolArg("-upnp", DEFAULT_UPNP));

  // Send and receive from sockets, accept connections
  socket_handler_thread = std::thread(std::bind(&TraceThread<void (*)()>, "net", &ThreadSocketHandler));

  // Initiate outbound connections from -addnode
  open_added_connections_thread =
      std::thread(std::bind(&TraceThread<void (*)()>, "addcon", &ThreadOpenAddedConnections));

  // Initiate outbound connections
  open_connections_thread = std::thread(std::bind(&TraceThread<void (*)()>, "opencon", &ThreadOpenConnections));

  // Process messages
  message_handler_thread = std::thread(std::bind(&TraceThread<void (*)()>, "msghand", &ThreadMessageHandler));

  // Dump network addresses
  scheduler.scheduleEvery(&DumpData, DUMP_ADDRESSES_INTERVAL);

  // ppcoin:mint proof-of-stake blocks in the background
  if (GetBoolArg("-staking", true))
    staking_handler_thread = std::thread(std::bind(&TraceThread<void (*)()>, "stakemint", &ThreadStakeMinter));
}

void InterruptNode() {
  net_interrupted = true;
  net_interrupt_cond.notify_all();
  messageHandlerCondition.notify_all();
}

bool StopNode() {
  LogPrintf("StopNode()\n");
  MapPort(false);
  if (semOutbound)
    for (int i = 0; i < MAX_OUTBOUND_CONNECTIONS; i++) semOutbound->post();

  if (fAddressesInitialized) {
    DumpData();
    fAddressesInitialized = false;
  }

  if (dns_address_seed_thread.joinable()) dns_address_seed_thread.join();
  if (socket_handler_thread.joinable()) socket_handler_thread.join();
  if (open_added_connections_thread.joinable()) open_added_connections_thread.join();
  if (open_connections_thread.joinable()) open_connections_thread.join();
  if (message_handler_thread.joinable()) message_handler_thread.join();
  if (staking_handler_thread.joinable()) staking_handler_thread.join();

  return true;
}

class CNetCleanup {
 public:
  CNetCleanup() {}

  ~CNetCleanup() {
    // Close sockets
    for (CNode* pnode : vNodes)
      if (pnode->hSocket != INVALID_SOCKET) CloseSocket(pnode->hSocket);
    for (ListenSocket& hListenSocket : vhListenSocket)
      if (hListenSocket.socket != INVALID_SOCKET)
        if (!CloseSocket(hListenSocket.socket))
          LogPrintf("CloseSocket(hListenSocket) failed with error %s\n", NetworkErrorString(WSAGetLastError()));

    // clean up some globals (to help leak detection)
    for (CNode* pnode : vNodes) delete pnode;
    for (CNode* pnode : vNodesDisconnected) delete pnode;
    vNodes.clear();
    vNodesDisconnected.clear();
    vhListenSocket.clear();
    delete semOutbound;
    semOutbound = nullptr;
    delete pnodeLocalHost;
    pnodeLocalHost = nullptr;

#ifdef WIN32
    // Shutdown Windows Sockets
    WSACleanup();
#endif
  }
} instance_of_cnetcleanup;

void CExplicitNetCleanup::callCleanup() {
  // Explicit call to destructor of CNetCleanup because it's not implicitly called
  // when the wallet is restarted from within the wallet itself.
  CNetCleanup* tmp = new CNetCleanup();
  delete tmp;  // Stroustrup's gonna kill me for that
}

void RelayTransaction(const CTransaction& tx) {
  CDataStream ss(SER_NETWORK, PROTOCOL_VERSION);
  ss.reserve(10000);
  ss << tx;
  RelayTransaction(tx, ss);
}

void RelayTransaction(const CTransaction& tx, const CDataStream& ss) {
  CInv inv(MSG_TX, tx.GetHash());
  {
    LOCK(cs_mapRelay);
    // Expire old relay messages
    while (!vRelayExpiration.empty() && vRelayExpiration.front().first < GetTime()) {
      mapRelay.erase(vRelayExpiration.front().second);
      vRelayExpiration.pop_front();
    }

    // Save original serialized message so newer versions are preserved
    mapRelay.insert(std::make_pair(inv, ss));
    vRelayExpiration.push_back(std::make_pair(GetTime() + 15 * 60, inv));
  }
  LOCK(cs_vNodes);
  for (CNode* pnode : vNodes) {
    if (!pnode->fRelayTxes) continue;
    LOCK(pnode->cs_filter);
    if (pnode->pfilter) {
      if (pnode->pfilter->IsRelevantAndUpdate(tx)) pnode->PushInventory(inv);
    } else
      pnode->PushInventory(inv);
  }
}

void RelayTransactionLockReq(const CTransaction& tx, bool relayToAll) {
  CInv inv(MSG_TXLOCK_REQUEST, tx.GetHash());

  // broadcast the new lock
  LOCK(cs_vNodes);
  for (CNode* pnode : vNodes) {
    if (!relayToAll && !pnode->fRelayTxes) continue;

    pnode->PushMessage("ix", tx);
  }
}

void RelayInv(CInv& inv) {
  LOCK(cs_vNodes);
  for (CNode* pnode : vNodes) {
    if (pnode->nNodeVersion >= ActiveProtocol()) pnode->PushInventory(inv);
  }
}

void CNode::RecordBytesRecv(uint64_t bytes) {
  LOCK(cs_totalBytesRecv);
  nTotalBytesRecv += bytes;
}

void CNode::RecordBytesSent(uint64_t bytes) {
  LOCK(cs_totalBytesSent);
  nTotalBytesSent += bytes;
}

uint64_t CNode::GetTotalBytesRecv() {
  LOCK(cs_totalBytesRecv);
  return nTotalBytesRecv;
}

uint64_t CNode::GetTotalBytesSent() {
  LOCK(cs_totalBytesSent);
  return nTotalBytesSent;
}

void CNode::Fuzz(int nChance) {
  if (!fSuccessfullyConnected) return;  // Don't fuzz initial handshake
  if (GetRand(nChance) != 0) return;    // Fuzz 1 of every nChance messages

  switch (GetRand(3)) {
    case 0:
      // xor a random byte with a random value:
      if (!ssSend.empty()) {
        CDataStream::size_type pos = GetRand(ssSend.size());
        ssSend[pos] ^= (uint8_t)(GetRand(256));
      }
      break;
    case 1:
      // delete a random byte:
      if (!ssSend.empty()) {
        CDataStream::size_type pos = GetRand(ssSend.size());
        ssSend.erase(ssSend.begin() + pos);
      }
      break;
    case 2:
      // insert a random byte at a random position
      {
        CDataStream::size_type pos = GetRand(ssSend.size());
        char ch = (char)GetRand(256);
        ssSend.insert(ssSend.begin() + pos, ch);
      }
      break;
  }
  // Chance of more than one change half the time:
  // (more changes exponentially less likely):
  Fuzz(2);
}

//
// CAddrDB
//

CAddrDB::CAddrDB() { pathAddr = GetDataDir() / "peers.dat"; }

bool CAddrDB::Write(const CAddrMan& addr) {
  // Generate random temporary filename
  uint16_t randv = 0;
  GetRandBytes((uint8_t*)&randv, sizeof(randv));
  std::string tmpfn = strprintf("peers.dat.%04x", randv);

  // serialize addresses, checksum data up to that point, then append csum
  CDataStream ssPeers(SER_DISK, CLIENT_VERSION);
  ssPeers << FLATDATA(Params().MessageStart());
  ssPeers << addr;
  uint256 hash = Hash(ssPeers.begin(), ssPeers.end());
  ssPeers << hash;

  // open output file, and associate with CAutoFile
  fs::path pathAddr = GetDataDir() / "peers.dat";
  FILE* file = fopen(pathAddr.string().c_str(), "wb");
  CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
  if (fileout.IsNull()) {
    LogPrintf("%s : Failed to open file %s", __func__, pathAddr.string());
    return true;
  }

  // Write and commit header, data
  try {
    fileout << ssPeers;
  } catch (std::exception& e) { return error("%s : Serialize or I/O error - %s", __func__, e.what()); }
  FileCommit(fileout.Get());
  fileout.fclose();

  return true;
}

bool CAddrDB::Read(CAddrMan& addr) {
  // open input file, and associate with CAutoFile
  FILE* file = fopen(pathAddr.string().c_str(), "rb");
  CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
  if (filein.IsNull()) {
    LogPrintf("%s : Failed to open file %s", __func__, pathAddr.string());
    return true;
  }

  // use file size to size memory buffer
  uint64_t fileSize = fs::file_size(pathAddr);
  uint64_t dataSize = fileSize - sizeof(uint256);
  // Don't try to resize to a negative number if file is small
  if (fileSize >= sizeof(uint256)) dataSize = fileSize - sizeof(uint256);
  vector<uint8_t> vchData;
  vchData.resize(dataSize);
  uint256 hashIn;

  // read data and checksum from file
  try {
    filein.read((char*)&vchData[0], dataSize);
    filein >> hashIn;
  } catch (std::exception& e) { return error("%s : Deserialize or I/O error - %s", __func__, e.what()); }
  filein.fclose();

  CDataStream ssPeers(vchData, SER_DISK, CLIENT_VERSION);

  // verify stored checksum matches input data
  uint256 hashTmp = Hash(ssPeers.begin(), ssPeers.end());
  if (hashIn != hashTmp) return error("%s : Checksum mismatch, data corrupted", __func__);

  uint8_t pchMsgTmp[4];
  try {
    // de-serialize file header (network specific magic number) and ..
    ssPeers >> FLATDATA(pchMsgTmp);

    // ... verify the network matches ours
    if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp)))
      return error("%s : Invalid network magic number", __func__);

    // de-serialize address data into one CAddrMan object
    ssPeers >> addr;
  } catch (std::exception& e) { return error("%s : Deserialize or I/O error - %s", __func__, e.what()); }

  return true;
}

uint32_t ReceiveFloodSize() { return 1000 * GetArg("-maxreceivebuffer", 5 * 1000); }
uint32_t SendBufferSize() { return 1000 * GetArg("-maxsendbuffer", 1 * 1000); }

CNode::CNode(SOCKET hSocketIn, CAddress addrIn, const std::string& addrNameIn, bool fInboundIn)
    : ssSend(SER_NETWORK, INIT_PROTO_VERSION), setAddrKnown(5000) {
  nServices = 0;
  hSocket = hSocketIn;
  nRecvVersion = INIT_PROTO_VERSION;
  nLastSend = 0;
  nLastRecv = 0;
  nSendBytes = 0;
  nRecvBytes = 0;
  nTimeConnected = GetTime();
  nTimeOffset = 0;
  addr = addrIn;
  addrName = addrNameIn == "" ? addr.ToStringIPPort() : addrNameIn;
  nNodeVersion = 0;
  strSubVer = "";
  fWhitelisted = false;
  fOneShot = false;
  fClient = false;  // set by version message
  fInbound = fInboundIn;
  fNetworkNode = false;
  fSuccessfullyConnected = false;
  fDisconnect = false;
  nRefCount = 0;
  nSendSize = 0;
  nSendOffset = 0;
  hashContinue.SetNull();
  nStartingHeight = -1;
  fGetAddr = false;
  fRelayTxes = false;
  setInventoryKnown.max_size(SendBufferSize() / 1000);
  pfilter = new CBloomFilter();
  nPingNonceSent = 0;
  nPingUsecStart = 0;
  nPingUsecTime = 0;
  fPingQueued = false;

  {
    LOCK(cs_nLastNodeId);
    id = nLastNodeId++;
  }

  if (fLogIPs)
    LogPrint(TessaLog::NET, "Added connection to %s peer=%d\n", addrName, id);
  else
    LogPrint(TessaLog::NET, "Added connection peer=%d\n", id);

  // Be shy and don't send version until we hear
  if (hSocket != INVALID_SOCKET && !fInbound) PushVersion();

  GetNodeSignals().InitializeNode.fire(GetId(), this);
}

CNode::~CNode() {
  CloseSocket(hSocket);

  if (pfilter) delete pfilter;

  // HACK : Check XXXX GetNodeSignals().FinalizeNode(GetId());
}

void CNode::AskFor(const CInv& inv) {
  if (mapAskFor.size() > MAPASKFOR_MAX_SZ) return;
  // We're using mapAskFor as a priority queue,
  // the key is the earliest time the request can be sent
  int64_t nRequestTime;
  limitedmap<CInv, int64_t>::const_iterator it = mapAlreadyAskedFor.find(inv);
  if (it != mapAlreadyAskedFor.end())
    nRequestTime = it->second;
  else
    nRequestTime = 0;
  LogPrint(TessaLog::NET, "askfor %s  %d (%s) peer=%d\n", inv.ToString(), nRequestTime,
           DateTimeStrFormat("%H:%M:%S", nRequestTime / 1000000), id);

  // Make sure not to reuse time indexes to keep things in the same order
  int64_t nNow = GetTimeMicros() - 1000000;
  static int64_t nLastTime;
  ++nLastTime;
  nNow = std::max(nNow, nLastTime);
  nLastTime = nNow;

  // Each retry is 2 minutes after the last
  nRequestTime = std::max(nRequestTime + 2 * 60 * 1000000, nNow);
  if (it != mapAlreadyAskedFor.end())
    mapAlreadyAskedFor.update(it, nRequestTime);
  else
    mapAlreadyAskedFor.insert(std::make_pair(inv, nRequestTime));
  mapAskFor.insert(std::make_pair(nRequestTime, inv));
}

void CNode::BeginMessage(const char* pszCommand) EXCLUSIVE_LOCK_FUNCTION(cs_vSend) {
  ENTER_CRITICAL_SECTION(cs_vSend);
  assert(ssSend.size() == 0);
  ssSend << CMessageHeader(pszCommand, 0);
  LogPrint(TessaLog::NET, "sending: %s ", SanitizeString(pszCommand));
}

void CNode::AbortMessage() UNLOCK_FUNCTION(cs_vSend) {
  ssSend.clear();

  LEAVE_CRITICAL_SECTION(cs_vSend);

  LogPrint(TessaLog::NET, "(aborted)\n");
}

void CNode::EndMessage() UNLOCK_FUNCTION(cs_vSend) {
  // The -*messagestest options are intentionally not documented in the help message,
  // since they are only used during development to debug the networking code and are
  // not intended for end-users.
  if (gArgs.IsArgSet("-dropmessagestest") && GetRand(atoi(gArgs.GetArg("-dropmessagestest", "2").c_str()) == 0)) {
    LogPrint(TessaLog::NET, "dropmessages DROPPING SEND MESSAGE\n");
    AbortMessage();
    return;
  }
  if (gArgs.IsArgSet("-fuzzmessagestest")) Fuzz(GetArg("-fuzzmessagestest", 10));

  if (ssSend.size() == 0) {
    LEAVE_CRITICAL_SECTION(cs_vSend);
    return;
  }

  // Set the size
  uint32_t nSize = ssSend.size() - CMessageHeader::HEADER_SIZE;
  memcpy((char*)&ssSend[CMessageHeader::MESSAGE_SIZE_OFFSET], &nSize, sizeof(nSize));

  // Set the checksum
  uint256 hash = Hash(ssSend.begin() + CMessageHeader::HEADER_SIZE, ssSend.end());
  uint32_t nChecksum = 0;
  memcpy(&nChecksum, &hash, sizeof(nChecksum));
  assert(ssSend.size() >= CMessageHeader::CHECKSUM_OFFSET + sizeof(nChecksum));
  memcpy((char*)&ssSend[CMessageHeader::CHECKSUM_OFFSET], &nChecksum, sizeof(nChecksum));

  LogPrint(TessaLog::NET, "(%d bytes) peer=%d\n", nSize, id);

  std::deque<CSerializeData>::iterator it = vSendMsg.insert(vSendMsg.end(), CSerializeData());
  ssSend.GetAndClear(*it);
  nSendSize += (*it).size();

  // If write queue empty, attempt "optimistic write"
  if (it == vSendMsg.begin()) SocketSendData(this);

  LEAVE_CRITICAL_SECTION(cs_vSend);
}

//
// CBanDB
//

CBanDB::CBanDB() { pathBanlist = GetDataDir() / "banlist.dat"; }

bool CBanDB::Write(const banmap_t& banSet) {
  // Generate random temporary filename
  uint16_t randv = 0;
  GetRandBytes((uint8_t*)&randv, sizeof(randv));
  std::string tmpfn = strprintf("banlist.dat.%04x", randv);

  // serialize banlist, checksum data up to that point, then append csum
  CDataStream ssBanlist(SER_DISK, CLIENT_VERSION);
  ssBanlist << FLATDATA(Params().MessageStart());
  ssBanlist << banSet;
  uint256 hash = Hash(ssBanlist.begin(), ssBanlist.end());
  ssBanlist << hash;

  // open temp output file, and associate with CAutoFile
  fs::path pathTmp = GetDataDir() / tmpfn;
  FILE* file = fopen(pathTmp.string().c_str(), "wb");
  CAutoFile fileout(file, SER_DISK, CLIENT_VERSION);
  if (fileout.IsNull()) {
    LogPrintf("%s: Failed to open file %s", __func__, pathTmp.string());
    return true;
  }
  // Write and commit header, data
  try {
    fileout << ssBanlist;
  } catch (const std::exception& e) { return error("%s: Serialize or I/O error - %s", __func__, e.what()); }
  FileCommit(fileout.Get());
  fileout.fclose();

  // replace existing banlist.dat, if any, with new banlist.dat.XXXX
  if (!RenameOver(pathTmp, pathBanlist)) return error("%s: Rename-into-place failed", __func__);

  return true;
}

bool CBanDB::Read(banmap_t& banSet) {
  // open input file, and associate with CAutoFile
  FILE* file = fopen(pathBanlist.string().c_str(), "rb");
  CAutoFile filein(file, SER_DISK, CLIENT_VERSION);
  if (filein.IsNull()) {
    LogPrintf("%s: Failed to open file %s", __func__, pathBanlist.string());
    return true;
  }
  // use file size to size memory buffer
  uint64_t fileSize = fs::file_size(pathBanlist);
  uint64_t dataSize = 0;
  // Don't try to resize to a negative number if file is small
  if (fileSize >= sizeof(uint256)) dataSize = fileSize - sizeof(uint256);
  vector<uint8_t> vchData;
  vchData.resize(dataSize);
  uint256 hashIn;

  // read data and checksum from file
  try {
    filein.read((char*)&vchData[0], dataSize);
    filein >> hashIn;
  } catch (const std::exception& e) { return error("%s: Deserialize or I/O error - %s", __func__, e.what()); }
  filein.fclose();

  CDataStream ssBanlist(vchData, SER_DISK, CLIENT_VERSION);

  // verify stored checksum matches input data
  uint256 hashTmp = Hash(ssBanlist.begin(), ssBanlist.end());
  if (hashIn != hashTmp) return error("%s: Checksum mismatch, data corrupted", __func__);

  uint8_t pchMsgTmp[4];
  try {
    // de-serialize file header (network specific magic number) and ..
    ssBanlist >> FLATDATA(pchMsgTmp);

    // ... verify the network matches ours
    if (memcmp(pchMsgTmp, Params().MessageStart(), sizeof(pchMsgTmp)))
      return error("%s: Invalid network magic number", __func__);

    // de-serialize address data into one CAddrMan object
    ssBanlist >> banSet;
  } catch (const std::exception& e) { return error("%s: Deserialize or I/O error - %s", __func__, e.what()); }

  return true;
}

void SaveJsonBanlist(banmap_t& banmap) {
  // New Create json file output for reference checking
  nlohmann::json json_data;
  for (auto& s : banmap) {
    std::cout << "s = " << s.first.ToString() << " : " << s.second.banReasonToString() << "\n";
    json_data[s.first.ToString()] = s.second.banReasonToString();
  }
  std::ofstream file_;
  fs::path jsonpath = GetDataDir() / "banlist.json";
  file_.open(jsonpath.string(), std::ofstream::out);
  file_ << json_data.dump(4);
  file_.close();
}

void DumpBanlist() {
  CNode::SweepBanned();  // clean unused entries (if bantime has expired)

  if (!CNode::BannedSetIsDirty()) return;

  int64_t nStart = GetTimeMillis();

  CBanDB bandb;
  banmap_t banmap;
  CNode::GetBanned(banmap);
  if (bandb.Write(banmap)) { CNode::SetBannedSetDirty(false); }

  SaveJsonBanlist(banmap);

  LogPrint(TessaLog::NET, "Flushed %d banned node ips/subnets to banlist.dat  %dms\n", banmap.size(),
           GetTimeMillis() - nStart);
}
