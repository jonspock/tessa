// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "walletdb.h"
#include "fs.h"
#include "fs_utils.h"
#include "main.h"
#include "protocol.h"
#include "serialize.h"
#include "sync.h"
#include "txdb.h"
#include "util.h"
#include "utiltime.h"
#include "validationstate.h"
#include "wallet.h"
#include "wallettx.h"

//#include <thread>
#include <fstream>

using namespace std;
using namespace bls;

static uint64_t nAccountingEntryNumber = 0;

//
// CWalletDB
//
CWalletDB gWalletDB;

bool CWalletDB::WriteName(const string& strAddress, const string& strName) {
  return Write(make_pair(string("name"), strAddress), strName);
}

bool CWalletDB::EraseName(const string& strAddress) {
  // This should only be used for sending addresses, never for receiving addresses,
  // receiving addresses must always have an address book entry if they're not change return.
  return Erase(make_pair(string("name"), strAddress));
}

bool CWalletDB::WritePurpose(const string& strAddress, const string& strPurpose) {
  return Write(make_pair(string("purpose"), strAddress), strPurpose);
}

bool CWalletDB::ErasePurpose(const string& strPurpose) { return Erase(make_pair(string("purpose"), strPurpose)); }

bool CWalletDB::WriteTx(uint256 hash, const CWalletTx& wtx) {
  return Write(std::make_pair(std::string("tx"), hash), wtx);
}

bool CWalletDB::EraseTx(uint256 hash) { return Erase(std::make_pair(std::string("tx"), hash)); }

bool CWalletDB::WriteCryptedKey(const CPubKey& vchPubKey, const std::vector<uint8_t>& vchCryptedSecret,
                                const CKeyMetadata& keyMeta) {
  if (!Write(std::make_pair(std::string("keymeta"), vchPubKey), keyMeta)) return false;
  if (!Write(std::make_pair(std::string("ckey"), vchPubKey), vchCryptedSecret, false)) return false;
  return true;
}

bool CWalletDB::ReadMasterKey(uint32_t nID, CMasterKey& kMasterKey) {
  return Read(std::make_pair(std::string("mkey"), nID), kMasterKey);
}
bool CWalletDB::WriteMasterKey(uint32_t nID, const CMasterKey& kMasterKey) {
  return Write(std::make_pair(std::string("mkey"), nID), kMasterKey, true);
}

bool CWalletDB::WriteCScript(const uint160& hash, const CScript& redeemScript) {
  return Write(std::make_pair(std::string("cscript"), hash), redeemScript, false);
}

bool CWalletDB::WriteWatchOnly(const CScript& dest) { return Write(std::make_pair(std::string("watchs"), dest), '1'); }

bool CWalletDB::EraseWatchOnly(const CScript& dest) { return Erase(std::make_pair(std::string("watchs"), dest)); }

bool CWalletDB::WriteMultiSig(const CScript& dest) { return Write(std::make_pair(std::string("multisig"), dest), '1'); }

bool CWalletDB::EraseMultiSig(const CScript& dest) { return Erase(std::make_pair(std::string("multisig"), dest)); }

bool CWalletDB::WriteBestBlock(const CBlockLocator& locator) { return Write(std::string("bestblock"), locator); }

bool CWalletDB::ReadBestBlock(CBlockLocator& locator) { return Read(std::string("bestblock"), locator); }

bool CWalletDB::WriteOrderPosNext(int64_t nOrderPosNext) { return Write(std::string("orderposnext"), nOrderPosNext); }

// presstab HyperStake
bool CWalletDB::WriteStakeSplitThreshold(uint64_t nStakeSplitThreshold) {
  return Write(std::string("stakeSplitThreshold"), nStakeSplitThreshold);
}

// presstab HyperStake
bool CWalletDB::WriteMultiSend(std::vector<std::pair<std::string, int> > vMultiSend) {
  bool ret = true;
  for (uint32_t i = 0; i < vMultiSend.size(); i++) {
    std::pair<std::string, int> pMultiSend;
    pMultiSend = vMultiSend[i];
    if (!Write(std::make_pair(std::string("multisend"), i), pMultiSend, true)) ret = false;
  }
  return ret;
}
// presstab HyperStake
bool CWalletDB::EraseMultiSend(std::vector<std::pair<std::string, int> > vMultiSend) {
  bool ret = true;
  for (uint32_t i = 0; i < vMultiSend.size(); i++) {
    std::pair<std::string, int> pMultiSend;
    pMultiSend = vMultiSend[i];
    if (!Erase(std::make_pair(std::string("multisend"), i))) ret = false;
  }
  return ret;
}
// presstab HyperStake
bool CWalletDB::WriteMSettings(bool fMultiSendStake, bool fObsolete, int nLastMultiSendHeight) {
  std::pair<bool, bool> enabledMS(fMultiSendStake, false);
  std::pair<std::pair<bool, bool>, int> pSettings(enabledMS, nLastMultiSendHeight);

  return Write(std::string("msettingsv2"), pSettings, true);
}
// presstab HyperStake
bool CWalletDB::WriteMSDisabledAddresses(std::vector<std::string> vDisabledAddresses) {
  bool ret = true;
  for (uint32_t i = 0; i < vDisabledAddresses.size(); i++) {
    if (!Write(std::make_pair(std::string("mdisabled"), i), vDisabledAddresses[i])) ret = false;
  }
  return ret;
}
// presstab HyperStake
bool CWalletDB::EraseMSDisabledAddresses(std::vector<std::string> vDisabledAddresses) {
  bool ret = true;
  for (uint32_t i = 0; i < vDisabledAddresses.size(); i++) {
    if (!Erase(std::make_pair(std::string("mdisabled"), i))) ret = false;
  }
  return ret;
}
bool CWalletDB::WriteAutoCombineSettings(bool fEnable, CAmount nCombineThreshold) {
  std::pair<bool, CAmount> pSettings;
  pSettings.first = fEnable;
  pSettings.second = nCombineThreshold;
  return Write(std::string("autocombinesettings"), pSettings, true);
}

bool CWalletDB::WriteDefaultKey(const CPubKey& vchPubKey) { return Write(std::string("defaultkey"), vchPubKey); }

bool CWalletDB::ReadPool(int64_t nPool, CKeyPool& keypool) {
  return Read(std::make_pair(std::string("pool"), nPool), keypool);
}

bool CWalletDB::WritePool(int64_t nPool, const CKeyPool& keypool) {
  return Write(std::make_pair(std::string("pool"), nPool), keypool);
}

bool CWalletDB::ErasePool(int64_t nPool) {
    if (Exists(std::make_pair(std::string("pool"), nPool))) {
        return Erase(std::make_pair(std::string("pool"), nPool));
    } else {
        return false;
    }
}

bool CWalletDB::WriteMinVersion(int nVersion) { return Write(std::string("minversion"), nVersion); }

bool CWalletDB::ReadAccount(const string& strAccount, CAccount& account) {
  account.SetNull();
  return Read(make_pair(string("acc"), strAccount), account);
}

bool CWalletDB::WriteAccount(const string& strAccount, const CAccount& account) {
  return Write(make_pair(string("acc"), strAccount), account);
}

bool CWalletDB::WriteAccountingEntry(const uint64_t nAccEntryNum, const CAccountingEntry& acentry) {
  return Write(std::make_pair(std::string("acentry"), std::make_pair(acentry.strAccount, nAccEntryNum)), acentry);
}

bool CWalletDB::WriteAccountingEntry_Backend(const CAccountingEntry& acentry) {
  return WriteAccountingEntry(++nAccountingEntryNumber, acentry);
}

CAmount CWalletDB::GetAccountCreditDebit(const string& strAccount) {
  list<CAccountingEntry> entries;
  ListAccountCreditDebit(strAccount, entries);

  CAmount nCreditDebit = 0;
  for (const CAccountingEntry& entry : entries) nCreditDebit += entry.nCreditDebit;

  return nCreditDebit;
}

void CWalletDB::ListAccountCreditDebit(const string& strAccount, list<CAccountingEntry>& entries) {
  bool fAllAccounts = (strAccount == "*");

  auto pcursor = GetCursor();
  if (!pcursor) throw runtime_error("CWalletDB::ListAccountCreditDebit() : cannot create DB cursor");
  uint32_t fFlags = MDB_SET_RANGE;
  while (true) {
    // Read next record
    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    if (fFlags == MDB_SET_RANGE)
      ssKey << std::make_pair(std::string("acentry"),
                              std::make_pair((fAllAccounts ? string("") : strAccount), uint64_t(0)));
    CDataStream ssValue(SER_DISK, CLIENT_VERSION);
    int ret = ReadAtCursor(pcursor, ssKey, ssValue, fFlags);
    fFlags = MDB_NEXT;
    if (ret == MDB_NOTFOUND)
      break;
    else if (ret != 0) {
      cursor_close(pcursor);
      throw runtime_error("CWalletDB::ListAccountCreditDebit() : error scanning DB");
    }

    // Unserialize
    string strType;
    ssKey >> strType;
    if (strType != "acentry") break;
    CAccountingEntry acentry;
    ssKey >> acentry.strAccount;
    if (!fAllAccounts && acentry.strAccount != strAccount) break;

    ssValue >> acentry;
    ssKey >> acentry.nEntryNo;
    entries.push_back(acentry);
  }

  cursor_close(pcursor);
}

DBErrors CWalletDB::ReorderTransactions(CWallet* pwallet) {
  LOCK(pwallet->cs_wallet);
  // Old wallets didn't have any defined order for transactions
  // Probably a bad idea to change the output of this

  // First: get all CWalletTx and CAccountingEntry into a sorted-by-time multimap.
  typedef pair<CWalletTx*, CAccountingEntry*> TxPair;
  typedef multimap<int64_t, TxPair> TxItems;
  TxItems txByTime;

  for (map<uint256, CWalletTx>::iterator it = pwallet->mapWallet.begin(); it != pwallet->mapWallet.end(); ++it) {
    CWalletTx* wtx = &((*it).second);
    txByTime.insert(make_pair(wtx->nTimeReceived, TxPair(wtx, (CAccountingEntry*)0)));
  }
  list<CAccountingEntry> acentries;
  ListAccountCreditDebit("", acentries);
  for (CAccountingEntry& entry : acentries) { txByTime.insert(make_pair(entry.nTime, TxPair((CWalletTx*)0, &entry))); }

  int64_t& nOrderPosNext = pwallet->nOrderPosNext;
  nOrderPosNext = 0;
  std::vector<int64_t> nOrderPosOffsets;
  for (TxItems::iterator it = txByTime.begin(); it != txByTime.end(); ++it) {
    CWalletTx* const pwtx = (*it).second.first;
    CAccountingEntry* const pacentry = (*it).second.second;
    int64_t& nOrderPos = (pwtx != 0) ? pwtx->nOrderPos : pacentry->nOrderPos;

    if (nOrderPos == -1) {
      nOrderPos = nOrderPosNext++;
      nOrderPosOffsets.push_back(nOrderPos);

      if (pwtx) {
        if (!WriteTx(pwtx->GetHash(), *pwtx)) return DB_LOAD_FAIL;
      } else if (!WriteAccountingEntry(pacentry->nEntryNo, *pacentry))
        return DB_LOAD_FAIL;
    } else {
      int64_t nOrderPosOff = 0;
      for (const int64_t& nOffsetStart : nOrderPosOffsets) {
        if (nOrderPos >= nOffsetStart) ++nOrderPosOff;
      }
      nOrderPos += nOrderPosOff;
      nOrderPosNext = std::max(nOrderPosNext, nOrderPos + 1);

      if (!nOrderPosOff) continue;

      // Since we're changing the order, write it back
      if (pwtx) {
        if (!WriteTx(pwtx->GetHash(), *pwtx)) return DB_LOAD_FAIL;
      } else if (!WriteAccountingEntry(pacentry->nEntryNo, *pacentry))
        return DB_LOAD_FAIL;
    }
  }
  WriteOrderPosNext(nOrderPosNext);

  return DB_LOAD_OK;
}

class CWalletScanState {
 public:
  uint32_t nKeys;
  uint32_t nCKeys;
  uint32_t nKeyMeta;
  bool fIsEncrypted;
  bool fAnyUnordered;
  int nFileVersion;
  vector<uint256> vWalletUpgrade;

  CWalletScanState() {
    nKeys = nCKeys = nKeyMeta = 0;
    fIsEncrypted = false;
    fAnyUnordered = false;
    nFileVersion = 0;
  }
};

bool ReadKeyValue(CWallet* pwallet, CDataStream& ssKey, CDataStream& ssValue, CWalletScanState& wss, string& strType,
                  string& strErr) {
  try {
    // Unserialize
    // Taking advantage of the fact that pair serialization
    // is just the two items serialized one after the other
    ssKey >> strType;
    if (strType == "name") {
      string strAddress;
      ssKey >> strAddress;
      ssValue >> pwallet->mapAddressBook[DecodeDestination(strAddress)].name;
    } else if (strType == "purpose") {
      string strAddress;
      ssKey >> strAddress;
      ssValue >> pwallet->mapAddressBook[DecodeDestination(strAddress)].purpose;
    } else if (strType == "tx") {
      uint256 hash;
      ssKey >> hash;
      CWalletTx wtx;
      ssValue >> wtx;
      CValidationState state;
      // false because there is no reason to go through the zerocoin checks for our own wallet
      if (!(CheckTransaction(wtx, state) && (wtx.GetHash() == hash) && state.IsValid())) return false;
      if (wtx.nOrderPos == -1) wss.fAnyUnordered = true;

      pwallet->AddToWallet(wtx, true);
    } else if (strType == "acentry") {
      string strAccount;
      ssKey >> strAccount;
      uint64_t nNumber;
      ssKey >> nNumber;
      if (nNumber > nAccountingEntryNumber) nAccountingEntryNumber = nNumber;

      if (!wss.fAnyUnordered) {
        CAccountingEntry acentry;
        ssValue >> acentry;
        if (acentry.nOrderPos == -1) wss.fAnyUnordered = true;
      }
    } else if (strType == "watchs") {
      CScript script;
      ssKey >> script;
      char fYes;
      ssValue >> fYes;
      if (fYes == '1') pwallet->LoadWatchOnly(script);

      // Watch-only addresses have no birthday information for now,
      // so set the wallet birthday to the beginning of time.
      pwallet->nTimeFirstKey = 1;
    } else if (strType == "multisig") {
      CScript script;
      ssKey >> script;
      char fYes;
      ssValue >> fYes;
      if (fYes == '1') pwallet->LoadMultiSig(script);

      // MultiSig addresses have no birthday information for now,
      // so set the wallet birthday to the beginning of time.
      pwallet->nTimeFirstKey = 1;
    } else if (strType == "mkey") {
      uint32_t nID;
      ssKey >> nID;
      CMasterKey kMasterKey;
      ssValue >> kMasterKey;
      if (pwallet->mapMasterKeys.count(nID) != 0) {
        strErr = strprintf("Error reading wallet database: duplicate CMasterKey id %u", nID);
        return false;
      }
      pwallet->mapMasterKeys[nID] = kMasterKey;
      if (pwallet->nMasterKeyMaxID < nID) pwallet->nMasterKeyMaxID = nID;
    } else if (strType == "ckey") {
      CPubKey vchPubKey;
      ssKey >> vchPubKey;
      vector<uint8_t> vchPrivKey;
      ssValue >> vchPrivKey;
      wss.nCKeys++;

      if (!pwallet->LoadCryptedKey(vchPubKey, vchPrivKey)) {
        strErr = "Error reading wallet database: LoadCryptedKey failed";
        return false;
      }
      wss.fIsEncrypted = true;
    } else if (strType == "keymeta") {
      CPubKey vchPubKey;
      ssKey >> vchPubKey;
      CKeyMetadata keyMeta;
      ssValue >> keyMeta;
      wss.nKeyMeta++;

      pwallet->LoadKeyMetadata(vchPubKey, keyMeta);

      // find earliest key creation time, as wallet birthday
      if (!pwallet->nTimeFirstKey || (keyMeta.nCreateTime < pwallet->nTimeFirstKey))
        pwallet->nTimeFirstKey = keyMeta.nCreateTime;
    } else if (strType == "defaultkey") {
      ssValue >> pwallet->vchDefaultKey;
    } else if (strType == "pool") {
      int64_t nIndex;
      ssKey >> nIndex;
      CKeyPool keypool;
      ssValue >> keypool;
      pwallet->setKeyPool.insert(nIndex);

      // If no metadata exists yet, create a default with the pool key's
      // creation time. Note that this may be overwritten by actually
      // stored metadata for that key later, which is fine.
      CKeyID keyid = keypool.vchPubKey.GetID();
      if (pwallet->mapKeyMetadata.count(keyid) == 0) pwallet->mapKeyMetadata[keyid] = CKeyMetadata(keypool.nTime);
    } else if (strType == "version") {
      ssValue >> wss.nFileVersion;
    } else if (strType == "cscript") {
      uint160 hash;
      ssKey >> hash;
      CScript script;
      ssValue >> script;
      if (!pwallet->LoadCScript(script)) {
        strErr = "Error reading wallet database: LoadCScript failed";
        return false;
      }
    } else if (strType == "orderposnext") {
      ssValue >> pwallet->nOrderPosNext;
    } else if (strType == "stakeSplitThreshold")  // presstab HyperStake
    {
      ssValue >> pwallet->nStakeSplitThreshold;
    } else if (strType == "multisend")  // presstab HyperStake
    {
      uint32_t i;
      ssKey >> i;
      std::pair<std::string, int> pMultiSend;
      ssValue >> pMultiSend;
      if (IsValidDestinationString(pMultiSend.first)) { pwallet->vMultiSend.push_back(pMultiSend); }
    } else if (strType == "msettingsv2")  // presstab HyperStake
    {
      std::pair<std::pair<bool, bool>, int> pSettings;
      ssValue >> pSettings;
      pwallet->fMultiSendStake = pSettings.first.first;
      pwallet->nLastMultiSendHeight = pSettings.second;
    } else if (strType == "mdisabled")  // presstab HyperStake
    {
      std::string strDisabledAddress;
      ssValue >> strDisabledAddress;
      pwallet->vDisabledAddresses.push_back(strDisabledAddress);
    } else if (strType == "autocombinesettings") {
      std::pair<bool, CAmount> pSettings;
      ssValue >> pSettings;
      pwallet->fCombineDust = pSettings.first;
      pwallet->nAutoCombineThreshold = pSettings.second;
    } else if (strType == "destdata") {
      std::string strAddress, strKey, strValue;
      ssKey >> strAddress;
      ssKey >> strKey;
      ssValue >> strValue;
      if (!pwallet->LoadDestData(DecodeDestination(strAddress), strKey, strValue)) {
        strErr = "Error reading wallet database: LoadDestData failed";
        return false;
      }
    } else if (strType == "hdchain") {
      CHDChain chain;
      ssValue >> chain;
      if (!pwallet->SetHDChain(chain, true)) {
        strErr = "Error reading wallet database: SetHDChain failed";
        return false;
      }
    }

  } catch (...) { return false; }
  return true;
}

static bool IsKeyType(string strType) { return (strType == "mkey" || strType == "ckey"); }

DBErrors CWalletDB::LoadWallet(CWallet* pwallet) {
  //pwallet->vchDefaultKey = CPubKey();
  CWalletScanState wss;
  bool fNoncriticalErrors = false;
  DBErrors result = DB_LOAD_OK;

  LOCK(pwallet->cs_wallet);
  int nMinVersion = 0;
  if (Read((string) "minversion", nMinVersion)) {
    if (nMinVersion > CLIENT_VERSION) return DB_TOO_NEW;
    pwallet->LoadMinVersion(nMinVersion);
  }

  // Get cursor
  auto pcursor = GetCursor();
  if (!pcursor) {
    LogPrintf("Error getting wallet database cursor\n");
    return DB_CORRUPT;
  }

  while (true) {
    // Read next record
    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    CDataStream ssValue(SER_DISK, CLIENT_VERSION);
    int ret = ReadAtCursor(pcursor, ssKey, ssValue);
    if (ret == MDB_NOTFOUND)
      break;
    else if (ret != 0) {
      LogPrintf("Error reading next record from wallet database\n");
      return DB_CORRUPT;
    }

    // Try to be tolerant of single corrupt records:
    string strType, strErr;
    if (!ReadKeyValue(pwallet, ssKey, ssValue, wss, strType, strErr)) {
      // losing keys is considered a catastrophic error, anything else
      // we assume the user can live with:
      if (IsKeyType(strType))
        result = DB_CORRUPT;
      else {
        // Leave other errors alone, if we try to fix them we might make things worse.
        fNoncriticalErrors = true;  // ... but do warn the user there is something wrong.
        if (strType == "tx")
          // Rescan if there is a bad transaction record:
          SoftSetBoolArg("-rescan", true);
      }
    }
    if (!strErr.empty()) LogPrintf("%s\n", strErr);
  }
  cursor_close(pcursor);

  if (fNoncriticalErrors && result == DB_LOAD_OK) result = DB_NONCRITICAL_ERROR;

  // Any wallet corruption at all: skip any rewriting or
  // upgrading, we don't want to make it worse.
  if (result != DB_LOAD_OK) return result;

  LogPrintf("nFileVersion = %d\n", wss.nFileVersion);

  LogPrintf("Keys: %u plaintext, %u encrypted, %u w/ metadata, %u total\n", wss.nKeys, wss.nCKeys, wss.nKeyMeta,
            wss.nKeys + wss.nCKeys);

  // nTimeFirstKey is only reliable if all keys have metadata
  if ((wss.nKeys + wss.nCKeys) != wss.nKeyMeta) pwallet->nTimeFirstKey = 1;  // 0 would be considered 'no value'

  for (uint256 hash : wss.vWalletUpgrade) WriteTx(hash, pwallet->mapWallet[hash]);

  if (wss.nFileVersion < CLIENT_VERSION)  // Update
    WriteVersion(CLIENT_VERSION);

  if (wss.fAnyUnordered) result = ReorderTransactions(pwallet);

  pwallet->laccentries.clear();
  ListAccountCreditDebit("*", pwallet->laccentries);
  for (CAccountingEntry& entry : pwallet->laccentries) {
    pwallet->wtxOrdered.insert(make_pair(entry.nOrderPos, CWallet::TxPair((CWalletTx*)0, &entry)));
  }

  return result;
}

DBErrors CWalletDB::FindWalletTx(CWallet* pwallet, vector<uint256>& vTxHash, vector<CWalletTx>& vWtx) {
  pwallet->vchDefaultKey = CPubKey();
  bool fNoncriticalErrors = false;
  DBErrors result = DB_LOAD_OK;

  LOCK(pwallet->cs_wallet);
  int nMinVersion = 0;
  if (Read((string) "minversion", nMinVersion)) {
    if (nMinVersion > CLIENT_VERSION) return DB_TOO_NEW;
    pwallet->LoadMinVersion(nMinVersion);
  }

  // Get cursor
  auto pcursor = GetCursor();
  if (!pcursor) {
    LogPrintf("Error getting wallet database cursor\n");
    return DB_CORRUPT;
  }

  while (true) {
    // Read next record
    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    CDataStream ssValue(SER_DISK, CLIENT_VERSION);
    int ret = ReadAtCursor(pcursor, ssKey, ssValue);
    if (ret == MDB_NOTFOUND)
      break;
    else if (ret != 0) {
      LogPrintf("Error reading next record from wallet database\n");
      return DB_CORRUPT;
    }

    string strType;
    ssKey >> strType;
    if (strType == "tx") {
      uint256 hash;
      ssKey >> hash;

      CWalletTx wtx;
      ssValue >> wtx;

      vTxHash.push_back(hash);
      vWtx.push_back(wtx);
    }
  }
  cursor_close(pcursor);

  if (fNoncriticalErrors && result == DB_LOAD_OK) result = DB_NONCRITICAL_ERROR;

  return result;
}

DBErrors CWalletDB::ZapWalletTx(CWallet* pwallet, vector<CWalletTx>& vWtx) {
  // build list of wallet TXs
  vector<uint256> vTxHash;
  DBErrors err = FindWalletTx(pwallet, vTxHash, vWtx);
  if (err != DB_LOAD_OK) return err;

  // erase each wallet TX
  for (uint256& hash : vTxHash) {
    if (!EraseTx(hash)) return DB_CORRUPT;
  }

  return DB_LOAD_OK;
}

void NotifyBacked(const CWallet& wallet, bool fSuccess, string strMessage) {
  LogPrint(TessaLog::NONE, strMessage.data());
  wallet.NotifyWalletBacked(fSuccess, strMessage);
}

bool CWalletDB::WriteDestData(const std::string& address, const std::string& key, const std::string& value) {
  return Write(std::make_pair(std::string("destdata"), std::make_pair(address, key)), value);
}

bool CWalletDB::EraseDestData(const std::string& address, const std::string& key) {
  return Erase(std::make_pair(std::string("destdata"), std::make_pair(address, key)));
}

bool CWalletDB::WriteHDChain(const CHDChain& chain) { return Write(std::string("hdchain"), chain); }


bool CWalletDB::WriteCurrentSeedHash(const uint256& hashSeed) { return Write(string("seedhash"), hashSeed); }

bool CWalletDB::ReadCurrentSeedHash(uint256& hashSeed) { return Read(string("seedhash"), hashSeed); }

