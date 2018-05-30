// Copyright (c) 2014-2016 The Dash developers
// Copyright (c) 2016-2018 The PIVX developers
// Copyright (c) 2018 The ClubChain developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "spork.h"
#include "base58.h"
#include "key.h"
#include "main.h"
#include "net.h"
#include "protocol.h"
#include "sporkdb.h"
#include "sync.h"
#include "timedata.h"
#include "util.h"

using namespace std;

class CSporkMessage;
class CSporkManager;

CSporkManager sporkManager;



// Club: on startup load spork values from previous session if they exist in the sporkDB
void CSporkManager::LoadSporksFromDB() {
    for (unsigned int i = 0; i < sporkList.size(); ++i) {
        // Since not all spork IDs are in use, we have to exclude undefined IDs
        SporkID sporkID = sporkList[i];
        std::string strSpork = GetSporkNameByID(sporkID);
        if (strSpork == "Unknown") continue;

        // attempt to read spork from sporkDB
        CSporkMessage spork;
        if (!pSporkDB->ReadSpork(sporkID, spork)) {
            LogPrintf("%s : no previous value for %s found in database\n", __func__, strSpork);
            continue;
        }

        // add spork to memory
        mapSporks[spork.GetHash()] = spork;
        mapSporksActive[spork.nSporkID] = spork;
        std::time_t result = spork.nValue;
        // If SPORK Value is greater than 1,000,000 assume it's actually a Date and then convert to a more readable format
        if (spork.nValue > 1000000) {
            LogPrintf("%s : loaded spork %s with value %d : %s", __func__, sporkManager.GetSporkNameByID(sporkID),
                      spork.nValue, std::ctime(&result));
        } else {
            LogPrintf("%s : loaded spork %s with value %d\n", __func__, sporkManager.GetSporkNameByID(sporkID),
                      spork.nValue);
        }
    }
}

void CSporkManager::ProcessSpork(CNode* pfrom, std::string& strCommand, CDataStream& vRecv) {
  //    if (fLiteMode) return 0;
  if (strCommand == "spork") {
    // LogPrintf("ProcessSpork::spork\n");
    CDataStream vMsg(vRecv);
    CSporkMessage spork;
    vRecv >> spork;

    if (chainActive.Tip() == NULL) return;

    // Ignore spork messages about unknown/deleted sporks
    SporkID id = sporkManager.GetSporkIDByInt(spork.nSporkID);
    std::string strSpork = sporkManager.GetSporkNameByID(id);
    if (strSpork == "Unknown") return;

    uint256 hash = spork.GetHash();
    if (mapSporksActive.count(spork.nSporkID)) {
      if (mapSporksActive[spork.nSporkID].nTimeSigned >= spork.nTimeSigned) {
        if (fDebug) LogPrintf("%s : seen %s block %d \n", __func__, hash.ToString(), chainActive.Tip()->nHeight);
        return;
      } else {
        if (fDebug)
          LogPrintf("%s : got updated spork %s block %d \n", __func__, hash.ToString(), chainActive.Tip()->nHeight);
      }
    }

    LogPrintf("%s : new %s ID %d Time %d bestHeight %d\n", __func__, hash.ToString(), spork.nSporkID, spork.nValue,
              chainActive.Tip()->nHeight);

    if (spork.nTimeSigned >= Params().NewSporkStart()) {
      if (!sporkManager.CheckSignature(spork, true)) {
        LogPrintf("%s : Invalid Signature\n", __func__);
        Misbehaving(pfrom->GetId(), 100);
        return;
      }
    }

    if (!sporkManager.CheckSignature(spork)) {
      LogPrintf("%s : Invalid Signature\n", __func__);
      Misbehaving(pfrom->GetId(), 100);
      return;
    }

    mapSporks[hash] = spork;
    mapSporksActive[spork.nSporkID] = spork;
    sporkManager.Relay(spork);

    // Club: add to spork database.
    pSporkDB->WriteSpork(id, spork);
  }
  if (strCommand == "getsporks") {
      for (auto& it : mapSporksActive) pfrom->PushMessage("spork", it.second);
  }
}

// grab the value of the spork on the network, or the default
int64_t CSporkManager::GetSporkValue(SporkID i) {
  int64_t r = -1;
  int64_t nSporkID = (int)i;
  if (mapSporksActive.count(nSporkID)) return mapSporksActive[nSporkID].nValue;
  return r;
}


// grab the spork value, and see if it's off
bool CSporkManager::IsSporkActive(SporkID nSporkID) {
  int64_t r = GetSporkValue(nSporkID);
  if (r == -1) return false;
  return r < GetTime();
}

void CSporkManager::ReprocessBlocks(int nBlocks) {
  std::map<uint256, int64_t>::iterator it = mapRejectedBlocks.begin();
  while (it != mapRejectedBlocks.end()) {
    // use a window twice as large as is usual for the nBlocks we want to reset
    if ((*it).second > GetTime() - (nBlocks * 60 * 5)) {
      BlockMap::iterator mi = mapBlockIndex.find((*it).first);
      if (mi != mapBlockIndex.end() && (*mi).second) {
        LOCK(cs_main);

        CBlockIndex* pindex = (*mi).second;
        LogPrintf("ReprocessBlocks - %s\n", (*it).first.ToString());

        CValidationState state;
        ReconsiderBlock(state, pindex);
      }
    }
    ++it;
  }

  CValidationState state;
  {
    LOCK(cs_main);
    DisconnectBlocksAndReprocess(nBlocks);
  }

  if (state.IsValid()) { ActivateBestChain(state); }
}

bool CSporkManager::CheckSignature(CSporkMessage& spork, bool fCheckSigner) {
  // note: need to investigate why this is failing
  std::string strMessage =
      std::to_string(spork.nSporkID) + std::to_string(spork.nValue) + std::to_string(spork.nTimeSigned);
  CPubKey pubkeynew(ParseHex(Params().SporkKey()));
  std::string errorMessage = "";

  /// HACK XXX
#warning "Need to fix VerifyMessage"
  bool fValidWithNewKey = true;
  // bool fValidWithNewKey = obfuScationSigner.VerifyMessage(pubkeynew, spork.vchSig,strMessage, errorMessage);

  if (fCheckSigner && !fValidWithNewKey) return false;
  return fValidWithNewKey;
}

bool CSporkManager::Sign(CSporkMessage& spork) { return true; }

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

bool CSporkManager::SetPrivKey(std::string strPrivKey) {
  CSporkMessage msg;

  // Test signing successful, proceed
  strMasterPrivKey = strPrivKey;

  Sign(msg);

  if (CheckSignature(msg, true)) {
    LogPrintf("CSporkManager::SetPrivKey - Successfully initialized as spork signer\n");
    return true;
  } else {
    return false;
  }
}

SporkID CSporkManager::GetSporkIDByName(std::string strName) {
    if (strName == "SPORK_PROTOCOL_ENFORCEMENT") return SporkID::SPORK_PROTOCOL_ENFORCEMENT;
    else return SporkID::SPORK_ZEROCOIN_MAINTENANCE_MODE;
}

SporkID CSporkManager::GetSporkIDByInt(int i) {
    if (i == 10001) return SporkID::SPORK_PROTOCOL_ENFORCEMENT;
    else return SporkID::SPORK_ZEROCOIN_MAINTENANCE_MODE;
}

std::string CSporkManager::GetSporkNameByID(SporkID id) {
    if (id == SporkID::SPORK_PROTOCOL_ENFORCEMENT) return "SPORK_PROTOCOL_ENFORCEMENT";
    if (id == SporkID::SPORK_ZEROCOIN_MAINTENANCE_MODE) return "SPORK_ZEROCOIN_MAINTENANCE_MODE";
    return "Unknown";
}
