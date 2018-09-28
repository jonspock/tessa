// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2016-2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "spork.h"
#include "ecdsa/key.h"
#include "key_io.h"
#include "main.h"
#include "protocol.h"
#include "sporkdb.h"
#include "utiltime.h"
#include "validationstate.h"

//#include "json/sporkdata.h"
using namespace std;
using namespace ecdsa;

// Public SporkKey
const std::string strSporkKey =
    "04B433E6598390C992F4F022F20D3B4CBBE691652EE7C48243B81701CBDB7CC7D7BF0EE09E154E6FCBF2043D65AF4E9E97B89B5DBAF830D8"
    "3B9B7F469A6C45A717";

// Global Spork Manager
CSporkManager gSporkManager;

// Tessa: on startup load spork values from previous session if they exist in the sporkDB
void CSporkManager::LoadSporksFromDB() {
  for (auto& sporkID : sporkList) {
    // Since not all spork IDs are in use, we have to exclude undefined IDs
    std::string strSpork = GetSporkNameByID(sporkID);
    if (strSpork == "Unknown") continue;

    // attempt to read spork from sporkDB
    CSporkMessage spork;
    if (!gSporkDB.ReadSpork(strSpork, spork)) {
      LogPrint(TessaLog::SPORK, "%s : no previous value for %s found in database\n", __func__, strSpork);
      continue;
    }

    // add spork to memory
    mapSporks[spork.GetHash()] = spork;
    mapSporksActive[spork.nSporkID] = spork;
    std::time_t result = spork.nValue;
    // If SPORK Value is greater than 1,000,000 assume it's actually a Date and then convert to a more readable format
    if (spork.nValue > 1000000) {
      LogPrint(TessaLog::SPORK, "%s : loaded spork %s with value %d : %s", __func__,
               gSporkManager.GetSporkNameByID(sporkID), spork.nValue, std::ctime(&result));
    } else {
      LogPrint(TessaLog::SPORK, "%s : loaded spork %s with value %d\n", __func__,
               gSporkManager.GetSporkNameByID(sporkID), spork.nValue);
    }
  }
}

void CSporkManager::ProcessSpork(CNode* pfrom, std::string& strCommand, CDataStream& vRecv) {
  //    if (fLiteMode) return 0;
  if (strCommand == "spork") {
    // LogPrint(TessaLog::SPORK,"ProcessSpork::spork\n");
    CDataStream vMsg(vRecv);
    CSporkMessage spork;
    vRecv >> spork;

    if (chainActive.Tip() == nullptr) return;

    // Ignore spork messages about unknown/deleted sporks
    SporkID id = GetSporkIDByInt(spork.nSporkID);
    std::string strSpork = GetSporkNameByID(id);
    if (strSpork == "Unknown") return;

    uint256 hash = spork.GetHash();
    if (mapSporksActive.count(spork.nSporkID)) {
      if (mapSporksActive[spork.nSporkID].nTimeSigned >= spork.nTimeSigned) {
        if (gArgs.IsArgSet("-debug"))
          LogPrint(TessaLog::SPORK, "%s : seen %s block %d \n", __func__, hash.ToString(), chainActive.Tip()->nHeight);
        return;
      } else {
        if (gArgs.IsArgSet("-debug"))
          LogPrint(TessaLog::SPORK, "%s : got updated spork %s block %d \n", __func__, hash.ToString(),
                   chainActive.Tip()->nHeight);
      }
    }

    LogPrint(TessaLog::SPORK, "%s : new %s ID %d Time %d bestHeight %d\n", __func__, hash.ToString(), spork.nSporkID,
             spork.nValue, chainActive.Tip()->nHeight);

    if (!CheckSignature(spork, true)) {
      LogPrint(TessaLog::SPORK, "%s : Invalid Signature\n", __func__);
      Misbehaving(pfrom->GetId(), 100);
      return;
    }

    mapSporks[hash] = spork;
    mapSporksActive[spork.nSporkID] = spork;
    Relay(spork);

    // Tessa: add to spork database.
    gSporkDB.WriteSpork(strSpork, spork);
  }

  if (strCommand == "getsporks") {
    for (auto& it : mapSporksActive) pfrom->PushMessage("spork", it.second);
  }
}

// grab the value of the spork on the network, or the default
int64_t CSporkManager::GetSporkValue(SporkID i) {
  int64_t nSporkID = (int)i;
  if (mapSporksActive.count(nSporkID)) return mapSporksActive[nSporkID].nValue;
  return -1;
}

// grab the spork value, and see if it's off
bool CSporkManager::IsSporkActive(SporkID nSporkID) {
  int64_t r = GetSporkValue(nSporkID);
  if (r == -1) return false;
  return r < GetTime();
}

bool CSporkManager::VerifyMessage(CPubKey pubkey, vector<uint8_t>& vchSig, const std::string& strMessage) {
  CHashWriter ss;
  ss << strMessageMagic;
  ss << strMessage;

  CPubKey pubkey2;
  if (!pubkey2.RecoverCompact(ss.GetHash(), vchSig)) return false;

  if (gArgs.IsArgSet("-debug") && pubkey2.GetID() != pubkey.GetID())
    LogPrint(TessaLog::SPORK, "VerifyMessage -- keys don't match: %s %s\n", pubkey2.GetID().ToString(),
             pubkey.GetID().ToString());

  return (pubkey2.GetID() == pubkey.GetID());
}

bool CSporkManager::CheckSignature(CSporkMessage& spork, bool fCheckSigner) {
  std::string strMessage =
      std::to_string(spork.nSporkID) + std::to_string(spork.nValue) + std::to_string(spork.nTimeSigned);
  CPubKey pubkeynew(ParseHex(strSporkKey));
  std::string errorMessage = "";

  bool fValidWithNewKey = VerifyMessage(pubkeynew, spork.vchSig, strMessage);
  if (fCheckSigner && !fValidWithNewKey) return false;
  return fValidWithNewKey;
}

bool CSporkManager::SetKey(const std::string& strSecret, CKey& key, CPubKey& pubkey) {
  key = DecodeSecret(strSecret);
  if (!key.IsValid()) return false;
  pubkey = key.GetPubKey();
  return true;
}

bool CSporkManager::SignMessage(const std::string& strMessage, vector<uint8_t>& vchSig, const CKey& key) {
  CHashWriter ss;
  ss << strMessageMagic;
  ss << strMessage;

  if (!key.SignCompact(ss.GetHash(), vchSig)) { return false; }

  return true;
}

bool CSporkManager::Sign(CSporkMessage& spork) {
  std::string strMessage =
      std::to_string(spork.nSporkID) + std::to_string(spork.nValue) + std::to_string(spork.nTimeSigned);

  CKey key;
  CPubKey pubkey;

  if (!SetKey(strMasterPrivKey, key, pubkey)) {
    LogPrint(TessaLog::SPORK, "Sign - ERROR: Invalid Spork Key\n");
    return false;
  }

  if (!SignMessage(strMessage, spork.vchSig, key)) {
    LogPrint(TessaLog::SPORK, "Sign - Spork Sign message failed");
    return false;
  }

  if (!VerifyMessage(pubkey, spork.vchSig, strMessage)) {
    LogPrint(TessaLog::SPORK, "Sign - Verify Spork message failed");
    return false;
  }

  return true;
}

bool CSporkManager::UpdateSpork(SporkID nSporkID, int64_t nValue) {
  CSporkMessage msg;
  msg.nSporkID = (int)nSporkID;
  msg.nValue = nValue;
  msg.nTimeSigned = GetTime();

  if (Sign(msg)) {
    Relay(msg);
    mapSporks[msg.GetHash()] = msg;
    mapSporksActive[(int)nSporkID] = msg;
    return true;
  }

  return false;
}

void CSporkManager::Relay(CSporkMessage& msg) {
  CInv inv(MSG_SPORK, msg.GetHash());
  RelayInv(inv);
}

bool CSporkManager::SetPrivKey(const std::string& strPrivKey) {
  CSporkMessage msg;

  // Test signing successful, proceed
  strMasterPrivKey = strPrivKey;

  Sign(msg);

  bool ok = (CheckSignature(msg, true));
  if (ok) LogPrint(TessaLog::SPORK, "CSporkManager::SetPrivKey - Successfully initialized as spork signer\n");
  return ok;
}

SporkID CSporkManager::GetSporkIDByName(const std::string& strName) {
  if (strName == "SPORK_PROTOCOL_ENFORCEMENT")
    return SporkID::SPORK_PROTOCOL_ENFORCEMENT;
  else
    return SporkID::SPORK_ZEROCOIN_MAINTENANCE_MODE;
}

SporkID CSporkManager::GetSporkIDByInt(int i) {
  if (i == 10001)
    return SporkID::SPORK_PROTOCOL_ENFORCEMENT;
  else
    return SporkID::SPORK_ZEROCOIN_MAINTENANCE_MODE;
}

std::string CSporkManager::GetSporkNameByID(SporkID id) {
  if (id == SporkID::SPORK_PROTOCOL_ENFORCEMENT) return "SPORK_PROTOCOL_ENFORCEMENT";
  if (id == SporkID::SPORK_ZEROCOIN_MAINTENANCE_MODE) return "SPORK_ZEROCOIN_MAINTENANCE_MODE";
  return "Unknown";
}
