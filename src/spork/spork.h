// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2016-2018 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "net.h"
#include "protocol.h"
#include "sync.h"

namespace ecdsa {
class CPubKey;
class CKey;
}  // namespace ecdsa

//
// Spork Class
// Keeps track of all of the network spork settings
//
class CSporkMessage {
 public:
  std::vector<uint8_t> vchSig;
  int nSporkID = 0;
  int64_t nValue = 0;
  int64_t nTimeSigned = 0;

  uint256 GetHash() const {
    uint256 n = Hash(BEGIN(nSporkID), END(nTimeSigned));
    return n;
  }

  ADD_SERIALIZE_METHODS

  template <typename Stream, typename Operation> inline void SerializationOp(Stream& s, Operation ser_action) {
    READWRITE(nSporkID);
    READWRITE(nValue);
    READWRITE(nTimeSigned);
    READWRITE(vchSig);
  }
};

enum class SporkID { SPORK_PROTOCOL_ENFORCEMENT = 1000, SPORK_ZEROCOIN_MAINTENANCE_MODE };
const std::vector<SporkID> sporkList = {SporkID::SPORK_PROTOCOL_ENFORCEMENT, SporkID::SPORK_ZEROCOIN_MAINTENANCE_MODE};

class CSporkManager {
 public:
 private:
  std::vector<uint8_t> vchSig;
  std::string strMasterPrivKey;
  std::map<uint256, CSporkMessage> mapSporks;
  std::map<int, CSporkMessage> mapSporksActive;

 public:
  CSporkManager() {}
  int count(const uint256& h) { return mapSporks.count(h); }
  CSporkMessage getSpork(const uint256& s) { return mapSporks[s]; }

  std::string GetSporkNameByID(SporkID id);
  SporkID GetSporkIDByName(const std::string& strName);
  SporkID GetSporkIDByInt(int i);
  int64_t GetSporkValue(SporkID i);
  bool IsSporkActive(SporkID nSporkID);

  bool UpdateSpork(SporkID nSporkID, int64_t nValue);
  bool SetPrivKey(const std::string& strPrivKey);
  bool SetKey(const std::string& strSecret, ecdsa::CKey& key, ecdsa::CPubKey& pubkey);
  bool SignMessage(const std::string& strMessage, std::vector<uint8_t>& vchSig, const ecdsa::CKey& key);
  bool VerifyMessage(ecdsa::CPubKey pubkey, std::vector<uint8_t>& vchSig, const std::string& strMessage);
  bool CheckSignature(CSporkMessage& spork, bool fCheckSigner = false);
  bool Sign(CSporkMessage& spork);
  void Relay(CSporkMessage& msg);

  void LoadSporksFromDB();
  void ProcessSpork(CNode* pfrom, std::string& strCommand, CDataStream& vRecv);
};

extern CSporkManager gSporkManager;
