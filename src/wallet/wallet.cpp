// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wallet.h"
#include "init.h"
#include "main.h"
#include "output.h"
#include "wallet_externs.h"
#include "wallettx.h"
#include "libzerocoin/key.h"
#include "bls/extkey.h"
#include "chain.h"
#include "chainparams.h"
#include "checkpoints.h"
#include "coincontrol.h"
#include "fs.h"
#include "kernel.h"
#include "net.h"
#include "reverse_iterate.h"
#include "script/script.h"
#include "script/sign.h"
#include "timedata.h"
#include "txdb.h"
#include "utilmoneystr.h"
#include "utiltime.h"
#include "validationstate.h"
#include "warnings.h"
#include <algorithm>
#include <cassert>
#include <random>
#include <thread>

#define KEY_RES_SIZE 200

using namespace std;
using namespace bls;

/**
 * Settings
 */
uint32_t nTxConfirmTarget = 1;
bool bSpendZeroConfChange = true;
bool bdisableSystemnotifications =
    false;  // Those bubbles can be annoying and slow down the UI when you get lots of trx
bool fSendFreeTransactions = false;
bool fPayAtLeastCustomFee = true;
int64_t nStartupTime = GetTime();  //!< Client startup time for use with automint
const uint32_t BIP32_HARDENED_KEY_LIMIT = 0x80000000;

static int64_t nReserveBalance = 0;

int64_t getReserveBalance() { return nReserveBalance; }
void setReserveBalance(int64_t b) { nReserveBalance = b; }

/** @defgroup mapWallet
 *
 * @{
 */

struct CompareValueOnly {
  bool operator()(const pair<CAmount, pair<const CWalletTx*, uint32_t> >& t1,
                  const pair<CAmount, pair<const CWalletTx*, uint32_t> >& t2) const {
    return t1.first < t2.first;
  }
};

std::string COutput::ToString() const {
  return strprintf("COutput(%s, %d, %d) [%s]", tx->GetHash().ToString(), i, nDepth, FormatMoney(tx->vout[i].nValue));
}

CAmount COutput::Value() const { return tx->vout[i].nValue; }

const CWalletTx* CWallet::GetWalletTx(const uint256& hash) const {
  LOCK(cs_wallet);
  const auto it = mapWallet.find(hash);
  if (it == mapWallet.end()) return nullptr;
  return &(it->second);
}

CPubKey CWallet::GenerateNewKey() {
  bool internal = true;       // FOR NOW XXXX HACK
  AssertLockHeld(cs_wallet);  // mapKeyMetadata
                              // bool fCompressed = true;    // default to compressed public keys

  CKey secret;
  // Create new metadata
  int64_t nCreationTime = GetTime();
  CKeyMetadata metadata(nCreationTime);

  // use HD key derivation - always enabled
  DeriveNewChildKey(metadata, secret, internal);

  CPubKey pubkey = secret.GetPubKey();
  assert(secret.VerifyPubKey(pubkey));

  mapKeyMetadata[pubkey.GetID()] = metadata;
  /////  UpdateTimeFirstKey(nCreationTime);

  ///???
  if (!AddKeyPubKeyWithDB(secret, pubkey)) { throw std::runtime_error(std::string(__func__) + ": AddKey failed"); }

  return pubkey;
}

uint256 CWallet::GetHDMasterKeySeed() {
  CKey key;
  // try to get the master key
  if (!GetKey(hdChain.masterKeyID, key)) { throw std::runtime_error(std::string(__func__) + ": Master key not found"); }
  uint256 seed = key.GetPrivKey_256();
  return seed;
}

void CWallet::DeriveNewChildKey(CKeyMetadata& metadata, CKey& secret, bool internal) {
  // for now we use a fixed keypath scheme of m/0'/0'/k
  // master key seed (256bit)
  CKey key;
  // hd master key
  CExtKey masterKey;
  // key at m/0'
  CExtKey accountKey;
  // key at m/0'/0' (external) or m/0'/1' (internal)
  CExtKey chainChildKey;
  // key at m/0'/0'/<n>'
  CExtKey childKey;

  // try to get the master key
  if (!GetKey(hdChain.masterKeyID, key)) { throw std::runtime_error(std::string(__func__) + ": Master key not found"); }

  masterKey.SetMaster(key); //.begin(), key.size());

  // derive m/0'
  // use hardened derivation (child keys >= 0x80000000 are hardened after
  // bip32)
  masterKey.Derive(accountKey, BIP32_HARDENED_KEY_LIMIT);

  // derive m/0'/0' (external chain) OR m/0'/1' (internal chain)
  //    assert(internal ? CanSupportFeature(FEATURE_HD_SPLIT) : true);
  accountKey.Derive(chainChildKey, BIP32_HARDENED_KEY_LIMIT + (internal ? 1 : 0));

  // derive child key at next index, skip keys already known to the wallet
  do {
    // always derive hardened keys
    // childIndex | BIP32_HARDENED_KEY_LIMIT = derive childIndex in hardened
    // child-index-range
    // example: 1 | BIP32_HARDENED_KEY_LIMIT == 0x80000001 == 2147483649
    if (internal) {
      chainChildKey.Derive(childKey, hdChain.nInternalChainCounter | BIP32_HARDENED_KEY_LIMIT);
      metadata.hdKeypath = "m/0'/1'/" + std::to_string(hdChain.nInternalChainCounter) + "'";
      hdChain.nInternalChainCounter++;
    } else {
      chainChildKey.Derive(childKey, hdChain.nExternalChainCounter | BIP32_HARDENED_KEY_LIMIT);
      metadata.hdKeypath = "m/0'/0'/" + std::to_string(hdChain.nExternalChainCounter) + "'";
      hdChain.nExternalChainCounter++;
    }
  } while (HaveKey(childKey.key.GetPubKey().GetID()));
  secret = childKey.key;
  metadata.hdMasterKeyID = hdChain.masterKeyID;
  // update the chain model in the database
  if (!gWalletDB.WriteHDChain(hdChain)) {
    throw std::runtime_error(std::string(__func__) + ": Writing HD chain model failed");
  }
}

bool CWallet::AddKeyPubKeyWithDB(const CKey& secret, const CPubKey& pubkey) {
  // mapKeyMetadata
  AssertLockHeld(cs_wallet);

  // CCryptoKeyStore has no concept of wallet databases, but calls
  // AddCryptedKey
  // which is overridden below.  To avoid flushes, the database handle is
  // tunneled through to it.
  if (!CCryptoKeyStore::AddKeyPubKey(secret, pubkey)) { return false; }

  // Check if we need to remove from watch-only.
  CScript script;
  script = GetScriptForDestination(pubkey.GetID());
  if (HaveWatchOnly(script)) { RemoveWatchOnly(script); }

  script = GetScriptForRawPubKey(pubkey);
  if (HaveWatchOnly(script)) { RemoveWatchOnly(script); }

  return true;
}

bool CWallet::AddKeyPubKey(const CKey& secret, const CPubKey& pubkey) {
  AssertLockHeld(cs_wallet);  // mapKeyMetadata
  if (!CCryptoKeyStore::AddKeyPubKey(secret, pubkey)) return false;

  // check if we need to remove from watch-only
  CScript script;
  script = GetScriptForDestination(pubkey.GetID());
  if (HaveWatchOnly(script)) RemoveWatchOnly(script);

  if (!fFileBacked) return true;
  return true;
}

bool CWallet::AddCryptedKey(const CPubKey& vchPubKey, const vector<uint8_t>& vchCryptedSecret) {
  if (!CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret)) return false;
  if (!fFileBacked) return true;
  {
    LOCK(cs_wallet);
    return gWalletDB.WriteCryptedKey(vchPubKey, vchCryptedSecret, mapKeyMetadata[vchPubKey.GetID()]);
  }
  return false;
}

bool CWallet::LoadKeyMetadata(const CPubKey& pubkey, const CKeyMetadata& meta) {
  AssertLockHeld(cs_wallet);  // mapKeyMetadata
  if (meta.nCreateTime && (!nTimeFirstKey || meta.nCreateTime < nTimeFirstKey)) nTimeFirstKey = meta.nCreateTime;

  mapKeyMetadata[pubkey.GetID()] = meta;
  return true;
}

bool CWallet::LoadCryptedKey(const CPubKey& vchPubKey, const std::vector<uint8_t>& vchCryptedSecret) {
  return CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret);
}

bool CWallet::AddCScript(const CScript& redeemScript) {
  if (!CCryptoKeyStore::AddCScript(redeemScript)) return false;
  if (!fFileBacked) return true;
  return gWalletDB.WriteCScript(Hash160(redeemScript), redeemScript);
}

bool CWallet::LoadCScript(const CScript& redeemScript) {
  /* A sanity check was added in pull #3843 to avoid adding redeemScripts
   * that never can be redeemed. However, old wallets may still contain
   * these. Do not add them to the wallet and warn. */
  if (redeemScript.size() > MAX_SCRIPT_ELEMENT_SIZE) {
    std::string strAddr = EncodeDestination(CTxDestination(CScriptID(redeemScript)));
    LogPrintf(
        "%s: Warning: This wallet contains a redeemScript of size %i which exceeds maximum size %i thus can never be "
        "redeemed. Do not use address %s.\n",
        __func__, redeemScript.size(), MAX_SCRIPT_ELEMENT_SIZE, strAddr);
    return true;
  }

  return CCryptoKeyStore::AddCScript(redeemScript);
}

bool CWallet::AddWatchOnly(const CScript& dest) {
  if (!CCryptoKeyStore::AddWatchOnly(dest)) return false;
  nTimeFirstKey = 1;  // No birthday information for watch-only keys.
  NotifyWatchonlyChanged.fire(true);
  if (!fFileBacked) return true;
  return gWalletDB.WriteWatchOnly(dest);
}

bool CWallet::RemoveWatchOnly(const CScript& dest) {
  AssertLockHeld(cs_wallet);
  if (!CCryptoKeyStore::RemoveWatchOnly(dest)) return false;
  if (!HaveWatchOnly()) NotifyWatchonlyChanged.fire(false);
  if (fFileBacked)
    if (!gWalletDB.EraseWatchOnly(dest)) return false;

  return true;
}

bool CWallet::LoadWatchOnly(const CScript& dest) { return CCryptoKeyStore::AddWatchOnly(dest); }

bool CWallet::AddMultiSig(const CScript& dest) {
  if (!CCryptoKeyStore::AddMultiSig(dest)) return false;
  nTimeFirstKey = 1;  // No birthday information
  NotifyMultiSigChanged.fire(true);
  if (!fFileBacked) return true;
  return gWalletDB.WriteMultiSig(dest);
}

bool CWallet::RemoveMultiSig(const CScript& dest) {
  AssertLockHeld(cs_wallet);
  if (!CCryptoKeyStore::RemoveMultiSig(dest)) return false;
  if (!HaveMultiSig()) NotifyMultiSigChanged.fire(false);
  if (fFileBacked)
    if (!gWalletDB.EraseMultiSig(dest)) return false;

  return true;
}

bool CWallet::LoadMultiSig(const CScript& dest) { return CCryptoKeyStore::AddMultiSig(dest); }
void CWallet::SetMaster(const CKeyingMaterial& vInMasterKey) { CCryptoKeyStore::SetMaster(vInMasterKey); }

bool CWallet::Unlock(const SecureString& strWalletPassphrase, bool anonymizeOnly) {
  SecureString strWalletPassphraseFinal;

  if (!IsLocked()) {
    fWalletUnlockAnonymizeOnly = anonymizeOnly;
    return true;
  }

  strWalletPassphraseFinal = strWalletPassphrase;

  CCrypter crypter;
  CKeyingMaterial vTempMasterKey;  // since multiple possible Master keys

  // Weird duplication here for now
  if (mapMasterKeys.empty()) {
    CMasterKey kMasterKey;
    if (gWalletDB.ReadMasterKey(1, kMasterKey)) {
        mapMasterKeys[1] = kMasterKey;
    } else {
        throw runtime_error("Problem reading MasterKey from Wallet dB");
    }
  }

  {
    LOCK(cs_wallet);
    for (const MasterKeyMap::value_type& pMasterKey : mapMasterKeys) {
      if (!crypter.SetKeyFromPassphrase(strWalletPassphraseFinal, pMasterKey.second.vchSalt,
                                        pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
        return false;
      if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vTempMasterKey)) continue;  // try another master key
      if (CCryptoKeyStore::Unlock(vTempMasterKey)) {
        fWalletUnlockAnonymizeOnly = anonymizeOnly;
        return true;
      }
    }
  }
  return false;
}

bool CWallet::ChangeWalletPassphrase(const SecureString& strOldWalletPassphrase,
                                     const SecureString& strNewWalletPassphrase) {
  bool fWasLocked = IsLocked();
  SecureString strOldWalletPassphraseFinal = strOldWalletPassphrase;

  {
    LOCK(cs_wallet);
    Lock();

    CCrypter crypter;
    CKeyingMaterial vTempMasterKey;
    for (MasterKeyMap::value_type& pMasterKey : mapMasterKeys) {
      if (!crypter.SetKeyFromPassphrase(strOldWalletPassphraseFinal, pMasterKey.second.vchSalt,
                                        pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
        return false;
      if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vTempMasterKey)) return false;
      if (CCryptoKeyStore::Unlock(vTempMasterKey)) {
        int64_t nStartTime = GetTimeMillis();
        crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt,
                                     pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod);
        pMasterKey.second.nDeriveIterations =
            pMasterKey.second.nDeriveIterations * (100 / ((double)(GetTimeMillis() - nStartTime)));

        nStartTime = GetTimeMillis();
        crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt,
                                     pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod);
        pMasterKey.second.nDeriveIterations =
            (pMasterKey.second.nDeriveIterations +
             pMasterKey.second.nDeriveIterations * 100 / ((double)(GetTimeMillis() - nStartTime))) /
            2;

        if (pMasterKey.second.nDeriveIterations < 25000) pMasterKey.second.nDeriveIterations = 25000;

        LogPrintf("Wallet passphrase changed to an nDeriveIterations of %i\n", pMasterKey.second.nDeriveIterations);

        if (!crypter.SetKeyFromPassphrase(strNewWalletPassphrase, pMasterKey.second.vchSalt,
                                          pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
          return false;
        if (!crypter.Encrypt(vTempMasterKey, pMasterKey.second.vchCryptedKey)) return false;
        gWalletDB.WriteMasterKey(pMasterKey.first, pMasterKey.second);
        if (fWasLocked) Lock();

        return true;
      }
    }
  }

  return false;
}

void CWallet::SetBestChain(const CBlockLocator& loc) { gWalletDB.WriteBestBlock(loc); }

set<uint256> CWallet::GetConflicts(const uint256& txid) const {
  set<uint256> result;
  AssertLockHeld(cs_wallet);

  const auto it = mapWallet.find(txid);
  if (it == mapWallet.end()) return result;
  const CWalletTx& wtx = it->second;

  std::pair<TxSpends::const_iterator, TxSpends::const_iterator> range;

  for (const CTxIn& txin : wtx.vin) {
    if (mapTxSpends.count(txin.prevout) <= 1) continue;  // No conflict if zero or one spends
    range = mapTxSpends.equal_range(txin.prevout);
    for (TxSpends::const_iterator it = range.first; it != range.second; ++it) result.insert(it->second);
  }
  return result;
}

void CWallet::SyncMetaData(pair<TxSpends::iterator, TxSpends::iterator> range) {
  // We want all the wallet transactions in range to have the same metadata as
  // the oldest (smallest nOrderPos).
  // So: find smallest nOrderPos:

  int nMinOrderPos = std::numeric_limits<int>::max();
  const CWalletTx* copyFrom = nullptr;
  for (TxSpends::iterator it = range.first; it != range.second; ++it) {
    const uint256& hash = it->second;
    int n = mapWallet[hash].nOrderPos;
    if (n < nMinOrderPos) {
      nMinOrderPos = n;
      copyFrom = &mapWallet[hash];
    }
  }
  // Now copy data from copyFrom to rest:
  for (TxSpends::iterator it = range.first; it != range.second; ++it) {
    const uint256& hash = it->second;
    CWalletTx* copyTo = &mapWallet[hash];
    if (copyFrom == copyTo) continue;
    copyTo->mapValue = copyFrom->mapValue;
    copyTo->vOrderForm = copyFrom->vOrderForm;
    // fTimeReceivedIsTxTime not copied on purpose
    // nTimeReceived not copied on purpose
    copyTo->nTimeSmart = copyFrom->nTimeSmart;
    copyTo->fFromMe = copyFrom->fFromMe;
    copyTo->strFromAccount = copyFrom->strFromAccount;
    // nOrderPos not copied on purpose
    // cached members not copied on purpose
  }
}

/**
 * Outpoint is spent if any non-conflicted transaction
 * spends it:
 */
bool CWallet::IsSpent(const uint256& hash, uint32_t n) const {
  const COutPoint outpoint(hash, n);
  pair<TxSpends::const_iterator, TxSpends::const_iterator> range;
  range = mapTxSpends.equal_range(outpoint);
  for (TxSpends::const_iterator it = range.first; it != range.second; ++it) {
    const uint256& wtxid = it->second;
    const auto mit = mapWallet.find(wtxid);
    if (mit != mapWallet.end() && mit->second.GetDepthInMainChain() >= 0) return true;  // Spent
  }
  return false;
}

void CWallet::AddToSpends(const COutPoint& outpoint, const uint256& wtxid) {
  mapTxSpends.insert(make_pair(outpoint, wtxid));
  pair<TxSpends::iterator, TxSpends::iterator> range;
  range = mapTxSpends.equal_range(outpoint);
  SyncMetaData(range);
}

void CWallet::AddToSpends(const uint256& wtxid) {
  assert(mapWallet.count(wtxid));
  CWalletTx& thisTx = mapWallet[wtxid];
  if (thisTx.IsCoinBase())  // Coinbases don't spend anything!
    return;

  for (const CTxIn& txin : thisTx.vin) AddToSpends(txin.prevout, wtxid);
}
// Creates a CMasterKey and adds it to mapMasterKeys for future use
// everything else in here is temporary
bool CWallet::SetupCrypter(const SecureString& strWalletPassphrase) {
  CKeyingMaterial vTempMasterKey;
  vTempMasterKey.resize(WALLET_CRYPTO_KEY_SIZE);
  GetStrongRandBytes(&vTempMasterKey[0], WALLET_CRYPTO_KEY_SIZE);

  CMasterKey kMasterKey;
  kMasterKey.vchSalt.resize(WALLET_CRYPTO_SALT_SIZE);
  GetStrongRandBytes(&kMasterKey.vchSalt[0], WALLET_CRYPTO_SALT_SIZE);

  CCrypter crypter;
  int64_t nStartTime = GetTimeMillis();
  crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, 25000, kMasterKey.nDerivationMethod);
  kMasterKey.nDeriveIterations = 2500000 / ((double)(GetTimeMillis() - nStartTime));

  nStartTime = GetTimeMillis();
  crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations,
                               kMasterKey.nDerivationMethod);

  kMasterKey.nDeriveIterations =
      (kMasterKey.nDeriveIterations + kMasterKey.nDeriveIterations * 100 / ((double)(GetTimeMillis() - nStartTime))) /
      2;

  if (kMasterKey.nDeriveIterations < 25000) kMasterKey.nDeriveIterations = 25000;

  LogPrintf("Encrypting Wallet with an nDeriveIterations of %i\n", kMasterKey.nDeriveIterations);

  // Sets up crypter with Key/IV for later use
  if (!crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations,
                                    kMasterKey.nDerivationMethod))
    return false;

  if (!crypter.Encrypt(vTempMasterKey, kMasterKey.vchCryptedKey)) return false;
  {
    LOCK(cs_wallet);
    mapMasterKeys[++nMasterKeyMaxID] = kMasterKey;
    gWalletDB.WriteMasterKey(nMasterKeyMaxID, kMasterKey);

    CMasterKey kkMasterKey;
    if (!gWalletDB.ReadMasterKey(nMasterKeyMaxID, kkMasterKey)) { LogPrintf("Problem reading back kMasterKey"); }
    // std::cout << "Set mapMasterKeys[" << nMasterKeyMaxID << "]\n";
  }

  CCryptoKeyStore::SetMaster(vTempMasterKey);

  // Generate a new master key.
  bls::CPubKey masterPubKey = pwalletMain->GenerateNewHDMasterKey();  // Also adds to DB
  if (!SetHDMasterKey(masterPubKey)) {
    throw std::runtime_error(std::string(__func__) + ": Storing master key failed");
  }

  NewKeyPool();
  return true;
}

int64_t CWallet::IncOrderPosNext() {
  AssertLockHeld(cs_wallet);  // nOrderPosNext
  int64_t nRet = nOrderPosNext++;
  gWalletDB.WriteOrderPosNext(nOrderPosNext);
  return nRet;
}

void CWallet::MarkDirty() {
  {
    LOCK(cs_wallet);
    for (auto& item : mapWallet) item.second.MarkDirty();
  }
}

bool CWallet::AddToWallet(const CWalletTx& wtxIn, bool fFromLoadWallet) {
  uint256 hash = wtxIn.GetHash();

  if (fFromLoadWallet) {
    mapWallet[hash] = wtxIn;
    CWalletTx& wtx = mapWallet[hash];
    wtx.BindWallet(this);
    wtxOrdered.insert(make_pair(wtx.nOrderPos, TxPair(&wtx, (CAccountingEntry*)nullptr)));
    AddToSpends(hash);
  } else {
    LOCK(cs_wallet);
    // Inserts only if not already there, returns tx inserted or tx found
    pair<map<uint256, CWalletTx>::iterator, bool> ret = mapWallet.insert(make_pair(hash, wtxIn));
    CWalletTx& wtx = (*ret.first).second;
    wtx.BindWallet(this);
    bool fInsertedNew = ret.second;
    if (fInsertedNew) {
      if (!wtx.nTimeReceived) wtx.nTimeReceived = GetAdjustedTime();
      wtx.nOrderPos = IncOrderPosNext();
      wtxOrdered.insert(make_pair(wtx.nOrderPos, TxPair(&wtx, (CAccountingEntry*)nullptr)));
      wtx.nTimeSmart = ComputeTimeSmart(wtx);
      AddToSpends(hash);
    }

    bool fUpdated = false;
    if (!fInsertedNew) {
      // Merge
      if (!wtxIn.hashBlock.IsNull() && wtxIn.hashBlock != wtx.hashBlock) {
        wtx.hashBlock = wtxIn.hashBlock;
        fUpdated = true;
      }
      if (wtxIn.nIndex != -1 && (wtxIn.vMerkleBranch != wtx.vMerkleBranch || wtxIn.nIndex != wtx.nIndex)) {
        wtx.vMerkleBranch = wtxIn.vMerkleBranch;
        wtx.nIndex = wtxIn.nIndex;
        fUpdated = true;
      }
      if (wtxIn.fFromMe && wtxIn.fFromMe != wtx.fFromMe) {
        wtx.fFromMe = wtxIn.fFromMe;
        fUpdated = true;
      }
    }

    //// debug print
    // LogPrintf("AddToWallet %s  %s%s\n", wtxIn.GetHash().ToString(), (fInsertedNew ? "new" : ""),
    //         (fUpdated ? "update" : ""));

    // Write to disk
    if (fInsertedNew || fUpdated)
      if (!wtx.WriteToDisk()) return false;

    // Break debit/credit balance caches:
    wtx.MarkDirty();

    // Notify UI of new or updated transaction
    NotifyTransactionChanged.fire(this, hash, fInsertedNew ? CT_NEW : CT_UPDATED);

    // notify an external script when a wallet transaction comes in or is updated
    std::string strCmd = GetArg("-walletnotify", "");

    if (!strCmd.empty()) {
      strCmd.replace(strCmd.find("%s"), 2, wtxIn.GetHash().GetHex());
      std::thread(runCommand, strCmd).detach();  // thread runs free
    }
  }
  return true;
}

/**
 * Add a transaction to the wallet, or update it.
 * pblock is optional, but should be provided if the transaction is known to be in a block.
 * If fUpdate is true, existing transactions will be updated.
 */
bool CWallet::AddToWalletIfInvolvingMe(const CTransaction& tx, const CBlock* pblock, bool fUpdate) {
  {
    AssertLockHeld(cs_wallet);
    bool fExisted = mapWallet.count(tx.GetHash()) != 0;
    if (fExisted && !fUpdate) return false;
    if (fExisted || IsMine(tx) || IsFromMe(tx)) {
      CWalletTx wtx(this, tx);
      // Get merkle branch if transaction was found in a block
      if (pblock) wtx.SetMerkleBranch(*pblock);
      return AddToWallet(wtx);
    }
  }
  return false;
}

void CWallet::SyncTransaction(const CTransaction& tx, const CBlock* pblock) {
  LOCK2(cs_main, cs_wallet);
  if (!AddToWalletIfInvolvingMe(tx, pblock, true)) return;  // Not one of ours

  // If a transaction changes 'conflicted' state, that changes the balance
  // available of the outputs it spends. So force those to be
  // recomputed, also:
  for (const CTxIn& txin : tx.vin) {
    if (mapWallet.count(txin.prevout.hash)) mapWallet[txin.prevout.hash].MarkDirty();
  }
}

void CWallet::EraseFromWallet(const uint256& hash) {
  if (!fFileBacked) return;
  {
    LOCK(cs_wallet);
    if (mapWallet.erase(hash)) gWalletDB.EraseTx(hash);
  }
  return;
}

isminetype CWallet::IsMine(const CTxIn& txin) const {
  {
    LOCK(cs_wallet);
    const auto mi = mapWallet.find(txin.prevout.hash);
    if (mi != mapWallet.end()) {
      const CWalletTx& prev = (*mi).second;
      if (txin.prevout.n < prev.vout.size()) return IsMine(prev.vout[txin.prevout.n]);
    }
  }
  return ISMINE_NO;
}

CAmount CWallet::GetDebit(const CTxIn& txin, const isminefilter& filter) const {
  {
    LOCK(cs_wallet);
    const auto mi = mapWallet.find(txin.prevout.hash);
    if (mi != mapWallet.end()) {
      const CWalletTx& prev = (*mi).second;
      if (txin.prevout.n < prev.vout.size())
        if (IsMine(prev.vout[txin.prevout.n]) & filter) return prev.vout[txin.prevout.n].nValue;
    }
  }
  return 0;
}

bool CWallet::IsChange(const CTxOut& txout) const {
  // TODO: fix handling of 'change' outputs. The assumption is that any
  // payment to a script that is ours, but is not in the address book
  // is change. That assumption is likely to break when we implement multisignature
  // wallets that return change back into a multi-signature-protected address;
  // a better way of identifying which outputs are 'the send' and which are
  // 'the change' will need to be implemented (maybe extend CWalletTx to remember
  // which output, if any, was change).
  if (::IsMine(*this, txout.scriptPubKey)) {
    CTxDestination address;
    if (!ExtractDestination(txout.scriptPubKey, address)) return true;

    LOCK(cs_wallet);
    if (!mapAddressBook.count(address)) return true;
  }
  return false;
}

/**
 * Scan the block chain (starting in pindexStart) for transactions
 * from or to us. If fUpdate is true, found transactions that already
 * exist in the wallet will be updated.
 */
int CWallet::ScanForWalletTransactions(CBlockIndex* pindexStart, bool fUpdate) {
  int ret = 0;
  int64_t nNow = GetTime();
  
  CBlockIndex* pindex = pindexStart;
  {
    LOCK2(cs_main, cs_wallet);

    // no need to read and scan block, if block was created before
    // our wallet birthday (as adjusted for block time variability)
    while (pindex && nTimeFirstKey && (pindex->GetBlockTime() < (nTimeFirstKey - 7200)))
      pindex = chainActive.Next(pindex);

    ShowProgress.fire(_("Rescanning..."),
                      0);  // show rescan progress in GUI as dialog or on splashscreen, if -rescan on startup
    double dProgressStart = Checkpoints::GuessVerificationProgress(pindex, false);
    double dProgressTip = Checkpoints::GuessVerificationProgress(chainActive.Tip(), false);
    set<uint256> setAddedToWallet;
    while (pindex) {
      if (pindex->nHeight % 100 == 0 && dProgressTip - dProgressStart > 0.0)
        ShowProgress.fire(
            _("Rescanning..."),
            std::max(1, std::min(99, (int)((Checkpoints::GuessVerificationProgress(pindex, false) - dProgressStart) /
                                           (dProgressTip - dProgressStart) * 100))));

      CBlock block;
      ReadBlockFromDisk(block, pindex);
      for (CTransaction& tx : block.vtx) {
        if (AddToWalletIfInvolvingMe(tx, &block, fUpdate)) ret++;
      }

      // If this is a zapwallettx, need to readd zkp
      pindex = chainActive.Next(pindex);
      if (GetTime() >= nNow + 60) {
        nNow = GetTime();
        LogPrintf("Still rescanning. At block %d. Progress=%f\n", pindex->nHeight,
                  Checkpoints::GuessVerificationProgress(pindex));
      }
    }
    ShowProgress.fire(_("Rescanning..."), 100);  // hide progress dialog in GUI
  }
  return ret;
}

void CWallet::ReacceptWalletTransactions() {
  LOCK2(cs_main, cs_wallet);
  for (auto& item : mapWallet) {
    const uint256& wtxid = item.first;
    CWalletTx& wtx = item.second;
    assert(wtx.GetHash() == wtxid);

    int nDepth = wtx.GetDepthInMainChain();

    if (!wtx.IsCoinBase() && !wtx.IsCoinStake() && nDepth < 0) {
      // Try to add to memory pool
      LOCK(mempool.cs);
      wtx.AcceptToMemoryPool(false);
    }
  }
}

void CWallet::ResendWalletTransactions() {
  // Do this infrequently and randomly to avoid giving away
  // that these are our transactions.
  if (GetTime() < nNextResend) return;
  bool fFirst = (nNextResend == 0);
  nNextResend = GetTime() + GetRand(30 * 60);
  if (fFirst) return;

  // Only do it if there's been a new block since last time
  if (nTimeBestReceived < nLastResend) return;
  nLastResend = GetTime();

  // Rebroadcast any of our txes that aren't in a block yet
  LogPrintf("ResendWalletTransactions()\n");
  {
    LOCK(cs_wallet);
    // Sort them in chronological order
    multimap<uint32_t, CWalletTx*> mapSorted;
    for (auto& item : mapWallet) {
      CWalletTx& wtx = item.second;
      // Don't rebroadcast until it's had plenty of time that
      // it should have gotten in already by now.
      if (nTimeBestReceived - (int64_t)wtx.nTimeReceived > 5 * 60) mapSorted.insert(make_pair(wtx.nTimeReceived, &wtx));
    }
    for (const auto& item : mapSorted) {
      CWalletTx& wtx = *item.second;
      wtx.RelayWalletTransaction();
    }
  }
}

/** @} */  // end of mapWallet

/** @defgroup Actions
 *
 * @{
 */

CAmount CWallet::GetBalance() const {
  CAmount nTotal = 0;
  {
    LOCK2(cs_main, cs_wallet);
    for (auto& it : mapWallet) {
      const CWalletTx* pcoin = &it.second;

      if (pcoin->IsTrusted()) nTotal += pcoin->GetAvailableCredit();
    }
  }

  return nTotal;
}

CAmount CWallet::GetUnlockedCoins() const {
  //    if (fLiteMode) return 0;

  CAmount nTotal = 0;
  {
    LOCK2(cs_main, cs_wallet);
    for (auto& it : mapWallet) {
      const CWalletTx* pcoin = &it.second;

      if (pcoin->IsTrusted() && pcoin->GetDepthInMainChain() > 0) nTotal += pcoin->GetUnlockedCredit();
    }
  }

  return nTotal;
}

CAmount CWallet::GetLockedCoins() const {
  //    if (fLiteMode) return 0;

  CAmount nTotal = 0;
  {
    LOCK2(cs_main, cs_wallet);
    for (auto& it : mapWallet) {
      const CWalletTx* pcoin = &it.second;
      if (pcoin->IsTrusted() && pcoin->GetDepthInMainChain() > 0) nTotal += pcoin->GetLockedCredit();
    }
  }

  return nTotal;
}

CAmount CWallet::GetUnconfirmedBalance() const {
  CAmount nTotal = 0;
  {
    LOCK2(cs_main, cs_wallet);
    for (auto& it : mapWallet) {
      const CWalletTx* pcoin = &it.second;
      if (!IsFinalTx(*pcoin) || (!pcoin->IsTrusted() && pcoin->GetDepthInMainChain() == 0))
        nTotal += pcoin->GetAvailableCredit();
    }
  }
  return nTotal;
}

CAmount CWallet::GetImmatureBalance() const {
  CAmount nTotal = 0;
  {
    LOCK2(cs_main, cs_wallet);
    for (auto& it : mapWallet) {
      const CWalletTx* pcoin = &it.second;
      nTotal += pcoin->GetImmatureCredit();
    }
  }
  return nTotal;
}

CAmount CWallet::GetWatchOnlyBalance() const {
  CAmount nTotal = 0;
  {
    LOCK2(cs_main, cs_wallet);
    for (auto& it : mapWallet) {
      const CWalletTx* pcoin = &it.second;
      if (pcoin->IsTrusted()) nTotal += pcoin->GetAvailableWatchOnlyCredit();
    }
  }

  return nTotal;
}

CAmount CWallet::GetUnconfirmedWatchOnlyBalance() const {
  CAmount nTotal = 0;
  {
    LOCK2(cs_main, cs_wallet);
    for (auto& it : mapWallet) {
      const CWalletTx* pcoin = &it.second;
      if (!IsFinalTx(*pcoin) || (!pcoin->IsTrusted() && pcoin->GetDepthInMainChain() == 0))
        nTotal += pcoin->GetAvailableWatchOnlyCredit();
    }
  }
  return nTotal;
}

CAmount CWallet::GetImmatureWatchOnlyBalance() const {
  CAmount nTotal = 0;
  {
    LOCK2(cs_main, cs_wallet);
    for (auto& it : mapWallet) {
      const CWalletTx* pcoin = &it.second;
      nTotal += pcoin->GetImmatureWatchOnlyCredit();
    }
  }
  return nTotal;
}

CAmount CWallet::GetLockedWatchOnlyBalance() const {
  CAmount nTotal = 0;
  {
    LOCK2(cs_main, cs_wallet);
    for (auto& it : mapWallet) {
      const CWalletTx* pcoin = &it.second;
      if (pcoin->IsTrusted() && pcoin->GetDepthInMainChain() > 0) nTotal += pcoin->GetLockedWatchOnlyCredit();
    }
  }
  return nTotal;
}

/**
 * populate vCoins with vector of available COutputs.
 */
void CWallet::AvailableCoins(vector<COutput>& vCoins, bool fOnlyConfirmed, const CCoinControl* coinControl,
                             bool fIncludeZeroValue, AvailableCoinsType nCoinType, bool fUseIX,
                             int nWatchonlyConfig) const {
  vCoins.clear();

  {
    LOCK2(cs_main, cs_wallet);
    for (auto& it : mapWallet) {
      const uint256& wtxid = it.first;
      const CWalletTx* pcoin = &it.second;

      if (!CheckFinalTx(*pcoin)) continue;

      if (fOnlyConfirmed && !pcoin->IsTrusted()) continue;

      if ((pcoin->IsCoinBase() || pcoin->IsCoinStake()) && pcoin->GetBlocksToMaturity() > 0) continue;

      int nDepth = pcoin->GetDepthInMainChain(false);
      // do not use IX for inputs that have less then 6 blockchain confirmations
      if (fUseIX && nDepth < 6) continue;

      // We should not consider coins which aren't at least in our mempool
      // It's possible for these to be conflicted via ancestors which we may never be able to detect
      if (nDepth == 0 && !pcoin->InMempool()) continue;

      for (uint32_t i = 0; i < pcoin->vout.size(); i++) {
        isminetype mine = IsMine(pcoin->vout[i]);
        if (IsSpent(wtxid, i)) continue;
        if (mine == ISMINE_NO) continue;

        if ((mine == ISMINE_MULTISIG || mine == ISMINE_SPENDABLE) && nWatchonlyConfig == 2) continue;

        if (mine == ISMINE_WATCH_ONLY && nWatchonlyConfig == 1) continue;

        if (IsLockedCoin(it.first, i)) continue;
        if (pcoin->vout[i].nValue <= 0 && !fIncludeZeroValue) continue;
        if (coinControl && coinControl->HasSelected() && !coinControl->fAllowOtherInputs &&
            !coinControl->IsSelected(it.first, i))
          continue;

        bool fIsSpendable = false;
        if ((mine & ISMINE_SPENDABLE) != ISMINE_NO) fIsSpendable = true;
        if ((mine & ISMINE_MULTISIG) != ISMINE_NO) fIsSpendable = true;

        vCoins.emplace_back(COutput(pcoin, i, nDepth, fIsSpendable));
      }
    }
  }
}

map<CTxDestination, vector<COutput> > CWallet::AvailableCoinsByAddress(bool fConfirmed, CAmount maxCoinValue) {
  vector<COutput> vCoins;
  AvailableCoins(vCoins, fConfirmed);

  map<CTxDestination, vector<COutput> > mapCoins;
  for (COutput& out : vCoins) {
    if (maxCoinValue > 0 && out.tx->vout[out.i].nValue > maxCoinValue) continue;

    CTxDestination address;
    if (!ExtractDestination(out.tx->vout[out.i].scriptPubKey, address)) continue;

    mapCoins[CTxDestination(address)].push_back(out);
  }

  return mapCoins;
}

static void ApproximateBestSubset(vector<pair<CAmount, pair<const CWalletTx*, uint32_t> > > vValue,
                                  const CAmount& nTotalLower, const CAmount& nTargetValue, vector<char>& vfBest,
                                  CAmount& nBest, int iterations = 1000) {
  vector<char> vfIncluded;

  vfBest.assign(vValue.size(), true);
  nBest = nTotalLower;

  FastRandomContext insecure_rand;

  for (int nRep = 0; nRep < iterations && nBest != nTargetValue; nRep++) {
    vfIncluded.assign(vValue.size(), false);
    CAmount nTotal = 0;
    bool fReachedTarget = false;
    for (int nPass = 0; nPass < 2 && !fReachedTarget; nPass++) {
      for (uint32_t i = 0; i < vValue.size(); i++) {
        // The solver here uses a randomized algorithm,
        // the randomness serves no real security purpose but is just
        // needed to prevent degenerate behavior and it is important
        // that the rng is fast. We do not use a constant random sequence,
        // because there may be some privacy improvement by making
        // the selection random.
        if (nPass == 0 ? insecure_rand.randbool() & 1 : !vfIncluded[i]) {
          nTotal += vValue[i].first;
          vfIncluded[i] = true;
          if (nTotal >= nTargetValue) {
            fReachedTarget = true;
            if (nTotal < nBest) {
              nBest = nTotal;
              vfBest = vfIncluded;
            }
            nTotal -= vValue[i].first;
            vfIncluded[i] = false;
          }
        }
      }
    }
  }
}

bool CWallet::SelectStakeCoins(std::list<std::unique_ptr<CStake> >& listInputs, CAmount nTargetAmount) {
  LOCK(cs_main);
  // Add Tessa
  vector<COutput> vCoins;
  AvailableCoins(vCoins, true, nullptr, false, STAKABLE_COINS);
  CAmount nAmountSelected = 0;
  if (GetBoolArg("-stake", true)) {
    for (const COutput& out : vCoins) {
      // make sure not to outrun target amount
      if (nAmountSelected + out.tx->vout[out.i].nValue > nTargetAmount) continue;

      // if zerocoinspend, then use the block time
      int64_t nTxTime = out.tx->GetTxTime();

      // check for min age
      if (GetAdjustedTime() - nTxTime < Params().StakeMinAge()) continue;

      // check that it is matured
      if (out.nDepth < (out.tx->IsCoinStake() ? Params().COINBASE_MATURITY() : 10)) continue;

      // add to our stake set
      nAmountSelected += out.tx->vout[out.i].nValue;

      std::unique_ptr<CStake> input(new CStake());
      input->SetInput((CTransaction)*out.tx, out.i);
      listInputs.emplace_back(std::move(input));
    }
  }

  return true;
}

bool CWallet::MintableCoins() {
  LOCK(cs_main);
  CAmount nBalance = GetBalance();

  // Regular Tessa
  if (nBalance > 0) {
    int64_t bal = 0;
    if (gArgs.IsArgSet("-reservebalance") && !ParseMoney(gArgs.GetArg("-reservebalance", ""), bal))
      return error("%s : invalid reserve balance amount", __func__);
    setReserveBalance(bal);
    if (nBalance <= bal) return false;

    vector<COutput> vCoins;
    AvailableCoins(vCoins, true);

    for (const COutput& out : vCoins) {
      int64_t nTxTime = out.tx->GetTxTime();
      if (GetAdjustedTime() - nTxTime > Params().StakeMinAge()) return true;
    }
  }

  return false;
}

bool CWallet::SelectCoinsMinConf(const CAmount& nTargetValue, int nConfMine, int nConfTheirs, vector<COutput> vCoins,
                                 set<pair<const CWalletTx*, uint32_t> >& setCoinsRet, CAmount& nValueRet) const {
  setCoinsRet.clear();
  nValueRet = 0;

  // List of values less than target
  pair<CAmount, pair<const CWalletTx*, uint32_t> > coinLowestLarger;
  coinLowestLarger.first = std::numeric_limits<CAmount>::max();
  coinLowestLarger.second.first = nullptr;
  vector<pair<CAmount, pair<const CWalletTx*, uint32_t> > > vValue;
  CAmount nTotalLower = 0;

#if __cplusplus < 201703L
  std::random_shuffle(vCoins.begin(), vCoins.end(), GetRandInt);
#else
  std::random_device rd;
  std::mt19937 g(rd());
  std::shuffle(vCoins.begin(), vCoins.end(), g);
#endif

  vValue.clear();
  nTotalLower = 0;
  for (const COutput& output : vCoins) {
    if (!output.fSpendable) continue;
    const CWalletTx* pcoin = output.tx;
    if (output.nDepth < (pcoin->IsFromMe(ISMINE_ALL) ? nConfMine : nConfTheirs)) continue;
    int i = output.i;
    CAmount n = pcoin->vout[i].nValue;

    pair<CAmount, pair<const CWalletTx*, uint32_t> > coin = make_pair(n, make_pair(pcoin, i));

    if (n == nTargetValue) {
      setCoinsRet.insert(coin.second);
      nValueRet += coin.first;
      return true;
    } else if (n < nTargetValue + COINCENT) {
      vValue.push_back(coin);
      nTotalLower += n;
    } else if (n < coinLowestLarger.first) {
      coinLowestLarger = coin;
    }
  }

  if (nTotalLower == nTargetValue) {
    for (uint32_t i = 0; i < vValue.size(); ++i) {
      setCoinsRet.insert(vValue[i].second);
      nValueRet += vValue[i].first;
    }
    return true;
  }

  if (nTotalLower < nTargetValue) {
    if (coinLowestLarger.second.first == nullptr)  // there is no input larger than nTargetValue
    {
      // we looked at everything possible and didn't find anything, no luck
      return false;
    }
    setCoinsRet.insert(coinLowestLarger.second);
    nValueRet += coinLowestLarger.first;
    return true;
  }

  // Solve subset sum by stochastic approximation
  sort(vValue.rbegin(), vValue.rend(), CompareValueOnly());
  vector<char> vfBest;
  CAmount nBest;

  ApproximateBestSubset(vValue, nTotalLower, nTargetValue, vfBest, nBest, 1000);
  if (nBest != nTargetValue && nTotalLower >= nTargetValue + COINCENT)
    ApproximateBestSubset(vValue, nTotalLower, nTargetValue + COINCENT, vfBest, nBest, 1000);

  // If we have a bigger coin and (either the stochastic approximation didn't find a good solution,
  //                                   or the next bigger coin is closer), return the bigger coin
  if (coinLowestLarger.second.first &&
      ((nBest != nTargetValue && nBest < nTargetValue + COINCENT) || coinLowestLarger.first <= nBest)) {
    setCoinsRet.insert(coinLowestLarger.second);
    nValueRet += coinLowestLarger.first;
  } else {
    string s = "CWallet::SelectCoinsMinConf best subset: ";
    for (uint32_t i = 0; i < vValue.size(); i++) {
      if (vfBest[i]) {
        setCoinsRet.insert(vValue[i].second);
        nValueRet += vValue[i].first;
        s += FormatMoney(vValue[i].first) + " ";
      }
    }
    LogPrintf("%s - total %s\n", s, FormatMoney(nBest));
  }

  return true;
}

bool CWallet::SelectCoins(const CAmount& nTargetValue, set<pair<const CWalletTx*, uint32_t> >& setCoinsRet,
                          CAmount& nValueRet, const CCoinControl* coinControl, AvailableCoinsType coin_type,
                          bool useIX) const {
  // Note: this function should never be used for "always free" tx types like dstx

  vector<COutput> vCoins;
  AvailableCoins(vCoins, true, coinControl, false, coin_type, useIX);

  // coin control -> return all selected outputs (we want all selected to go into the transaction for sure)
  if (coinControl && coinControl->HasSelected()) {
    for (const COutput& out : vCoins) {
      if (!out.fSpendable) continue;

      nValueRet += out.tx->vout[out.i].nValue;
      setCoinsRet.insert(make_pair(out.tx, out.i));
    }
    return (nValueRet >= nTargetValue);
  }

  return (SelectCoinsMinConf(nTargetValue, 1, 6, vCoins, setCoinsRet, nValueRet) ||
          SelectCoinsMinConf(nTargetValue, 1, 1, vCoins, setCoinsRet, nValueRet) ||
          (bSpendZeroConfChange && SelectCoinsMinConf(nTargetValue, 0, 1, vCoins, setCoinsRet, nValueRet)));
}

int CWallet::CountInputsWithAmount(CAmount nInputAmount) {
  CAmount nTotal = 0;
  {
    LOCK(cs_wallet);
    for (auto& it : mapWallet) {
      const CWalletTx* pcoin = &it.second;
      if (pcoin->IsTrusted()) {
        int nDepth = pcoin->GetDepthInMainChain(false);

        for (uint32_t i = 0; i < pcoin->vout.size(); i++) {
          COutput out = COutput(pcoin, i, nDepth, true);
          CTxIn vin = CTxIn(out.tx->GetHash(), out.i);

          if (out.tx->vout[out.i].nValue != nInputAmount) continue;
          continue;

          nTotal++;
        }
      }
    }
  }

  return nTotal;
}

bool CWallet::ConvertList(std::vector<CTxIn> vCoins, std::vector<CAmount>& vecAmounts) {
  for (CTxIn i : vCoins) {
    if (mapWallet.count(i.prevout.hash)) {
      CWalletTx& wtx = mapWallet[i.prevout.hash];
      if (i.prevout.n < wtx.vout.size()) { vecAmounts.push_back(wtx.vout[i.prevout.n].nValue); }
    } else {
      LogPrintf("ConvertList -- Couldn't find transaction\n");
    }
  }
  return true;
}

bool CWallet::CreateTransaction(const vector<pair<CScript, CAmount> >& vecSend, CWalletTx& wtxNew,
                                CReserveKey& reservekey, CAmount& nFeeRet, std::string& strFailReason,
                                const CCoinControl* coinControl, AvailableCoinsType coin_type, bool useIX,
                                CAmount nFeePay) {
  if (useIX && nFeePay < COINCENT) nFeePay = COINCENT;

  CAmount nValue = 0;

  for (const auto& s : vecSend) {
    if (nValue < 0) {
      strFailReason = _("Transaction amounts must be positive");
      return false;
    }
    nValue += s.second;
  }
  if (vecSend.empty() || nValue < 0) {
    strFailReason = _("Transaction amounts must be positive");
    return false;
  }

  wtxNew.fTimeReceivedIsTxTime = true;
  wtxNew.BindWallet(this);
  CMutableTransaction txNew;

  {
    LOCK2(cs_main, cs_wallet);
    {
      nFeeRet = 0;
      if (nFeePay > 0) nFeeRet = nFeePay;
      while (true) {
        txNew.vin.clear();
        txNew.vout.clear();
        wtxNew.fFromMe = true;

        CAmount nTotalValue = nValue + nFeeRet;
        double dPriority = 0;

        // vouts to the payees
        if (coinControl && !coinControl->fSplitBlock) {
          for (const auto& s : vecSend) {
            CTxOut txout(s.second, s.first);
            if (txout.IsDust(::minRelayTxFee)) {
              strFailReason = _("Transaction amount too small");
              return false;
            }
            txNew.vout.push_back(txout);
          }
        } else  // UTXO Splitter Transaction
        {
          int nSplitBlock;

          if (coinControl)
            nSplitBlock = coinControl->nSplitBlock;
          else
            nSplitBlock = 1;

          for (const auto& s : vecSend) {
            for (int i = 0; i < nSplitBlock; i++) {
              if (i == nSplitBlock - 1) {
                uint64_t nRemainder = s.second % nSplitBlock;
                txNew.vout.push_back(CTxOut((s.second / nSplitBlock) + nRemainder, s.first));
              } else
                txNew.vout.push_back(CTxOut(s.second / nSplitBlock, s.first));
            }
          }
        }

        // Choose coins to use
        set<pair<const CWalletTx*, uint32_t> > setCoins;
        CAmount nValueIn = 0;

        if (!SelectCoins(nTotalValue, setCoins, nValueIn, coinControl, coin_type, useIX)) {
          strFailReason = _("Insufficient funds.");
          return false;
        }

        for (auto pcoin : setCoins) {
          CAmount nCredit = pcoin.first->vout[pcoin.second].nValue;
          // The coin age after the next block (depth+1) is used instead of the current,
          // reflecting an assumption the user would accept a bit more delay for
          // a chance at a free transaction.
          // But mempool inputs might still be in the mempool, so their age stays 0
          int age = pcoin.first->GetDepthInMainChain();
          if (age != 0) age += 1;
          dPriority += (double)nCredit * age;
        }

        CAmount nChange = nValueIn - nValue - nFeeRet;

        if (nChange > 0) {
          // Fill a vout to ourself
          // TODO: pass in scriptChange instead of reservekey so
          // change transaction isn't always pay-to-coin-address
          CScript scriptChange;
          bool combineChange = false;

          // coin control: send change to custom address
          if (coinControl) {
            try {
              std::get<CNoDestination>(coinControl->destChange);
              scriptChange = GetScriptForDestination(coinControl->destChange);

              auto it = txNew.vout.begin();
              while (it != txNew.vout.end()) {
                if (scriptChange == it->scriptPubKey) {
                  it->nValue += nChange;
                  nChange = 0;
                  reservekey.ReturnKey();
                  combineChange = true;
                  break;
                }
                ++it;
              }
            } catch (std::bad_variant_access&) { LogPrintf("bad variant access"); }
          }

          // no coin control: send change to newly generated address
          else {
            // Note: We use a new key here to keep it from being obvious which side is the change.
            //  The drawback is that by not reusing a previous key, the change may be lost if a
            //  backup is restored, if the backup doesn't have the new private key for the change.
            //  If we reused the old key, it would be possible to add code to look for and
            //  rediscover unknown transactions that were written with keys of ours to recover
            //  post-backup change.

            // Reserve a new key pair from key pool
            CPubKey vchPubKey;
            bool ret;
            ret = reservekey.GetReservedKey(vchPubKey);
            assert(ret);  // should never fail, as we just unlocked

            scriptChange = GetScriptForDestination(vchPubKey.GetID());
          }

          if (!combineChange) {
            CTxOut newTxOut(nChange, scriptChange);

            // Never create dust outputs; if we would, just
            // add the dust to the fee.
            if (newTxOut.IsDust(::minRelayTxFee)) {
              nFeeRet += nChange;
              nChange = 0;
              reservekey.ReturnKey();
            } else {
              // Insert change txn at random position:
              auto position = txNew.vout.begin() + GetRandInt(txNew.vout.size() + 1);
              txNew.vout.insert(position, newTxOut);
            }
          }
        } else
          reservekey.ReturnKey();

        // Fill vin
        for (const auto& coin : setCoins) txNew.vin.push_back(CTxIn(coin.first->GetHash(), coin.second));

        // Sign
        int nIn = 0;
        for (const auto& coin : setCoins)
          if (!SignSignature(*this, *coin.first, txNew, nIn++)) {
            strFailReason = _("Signing transaction failed");
            return false;
          }

        // Embed the constructed transaction data in wtxNew.
        *static_cast<CTransaction*>(&wtxNew) = CTransaction(txNew);

        // Limit size
        uint32_t nBytes = ::GetSerializeSize(*(CTransaction*)&wtxNew);
        if (nBytes >= MAX_STANDARD_TX_SIZE) {
          strFailReason = _("Transaction too large");
          return false;
        }
        dPriority = wtxNew.ComputePriority(dPriority, nBytes);

        // Can we complete this as a free transaction?
        if (fSendFreeTransactions && nBytes <= MAX_FREE_TRANSACTION_CREATE_SIZE) {
          // Not enough fee: enough priority?
          double dPriorityNeeded = mempool.estimatePriority(nTxConfirmTarget);
          // Not enough mempool history to estimate: use hard-coded AllowFree.
          if (dPriorityNeeded <= 0 && AllowFree(dPriority)) break;

          // Small enough, and priority high enough, to send for free
          if (dPriorityNeeded > 0 && dPriority >= dPriorityNeeded) break;
        }

        CAmount nFeeNeeded = max(nFeePay,minTxFee);

        // If we made it here and we aren't even able to meet the relay fee on the next pass, give up
        // because we must be at the maximum allowed fee.
        if (nFeeNeeded < ::minRelayTxFee) {
          strFailReason = _("Transaction too large for fee policy");
          return false;
        }

        if (nFeeRet >= nFeeNeeded)  // Done, enough fee included
          break;

        // Include more fee and try again.
        nFeeRet = nFeeNeeded;
        continue;
      }
    }
  }
  return true;
}

bool CWallet::CreateTransaction(CScript scriptPubKey, const CAmount& nValue, CWalletTx& wtxNew, CReserveKey& reservekey,
                                CAmount& nFeeRet, std::string& strFailReason, const CCoinControl* coinControl,
                                AvailableCoinsType coin_type, bool useIX, CAmount nFeePay) {
  vector<pair<CScript, CAmount> > vecSend;
  vecSend.push_back(make_pair(scriptPubKey, nValue));
  return CreateTransaction(vecSend, wtxNew, reservekey, nFeeRet, strFailReason, coinControl, coin_type, useIX, nFeePay);
}

// ppcoin: create coin stake transaction
bool CWallet::CreateCoinStake(const CKeyStore& keystore, uint32_t nBits, int64_t nSearchInterval,
                              CMutableTransaction& txNew, uint32_t& nTxNewTime) {
  // The following split & combine thresholds are important to security
  // Should not be adjusted if you don't understand the consequences
  // int64_t nCombineThreshold = 0;
  txNew.vin.clear();
  txNew.vout.clear();

  // Mark coin stake transaction
  CScript scriptEmpty;
  scriptEmpty.clear();
  txNew.vout.push_back(CTxOut(0, scriptEmpty));

  // Choose coins to use
  CAmount nBalance = GetBalance();

  int64_t bal;
  if (gArgs.IsArgSet("-reservebalance") && !ParseMoney(gArgs.GetArg("-reservebalance", ""), bal))
    return error("CreateCoinStake : invalid reserve balance amount");
  setReserveBalance(bal);
  if (nBalance > 0 && nBalance <= bal) return false;

  // Get the list of stakable inputs
  std::list<std::unique_ptr<CStake> > listInputs;
  if (!SelectStakeCoins(listInputs, nBalance - bal)) return false;

  if (listInputs.empty()) return false;

  if (GetAdjustedTime() - chainActive.Tip()->GetBlockTime() < 60) MilliSleep(10000);

  CAmount nCredit = 0;
  CScript scriptPubKeyKernel;
  bool fKernelFound = false;
  for (std::unique_ptr<CStake>& stakeInput : listInputs) {
    // Make sure the wallet is unlocked and shutdown hasn't been requested
    if (IsLocked() || ShutdownRequested()) return false;

    // make sure that enough time has elapsed between
    CBlockIndex* pindex = stakeInput->GetIndexFrom();
    if (!pindex || pindex->nHeight < 1) {
      LogPrintf("*** no pindexfrom\n");
      continue;
    }

    // Read block header
    CBlockHeader block = pindex->GetBlockHeader();
    uint256 hashProofOfStake;
    nTxNewTime = GetAdjustedTime();

    // iterates each utxo inside of CheckStakeKernelHash()
    if (Stake(stakeInput.get(), nBits, block.GetBlockTime(), nTxNewTime, hashProofOfStake)) {
      LOCK(cs_main);
      // Double check that this will pass time requirements
      if (nTxNewTime <= chainActive.Tip()->GetMedianTimePast()) {
        LogPrintf("CreateCoinStake() : kernel found, but it is too far in the past \n");
        continue;
      }

      // Found a kernel
      LogPrintf("CreateCoinStake : kernel found\n");
      nCredit += stakeInput->GetValue();

      // Add reward
      nCredit += GetBlockValue(chainActive.Height() + 1);

      // Create the output transaction(s)
      vector<CTxOut> vout;
      if (!stakeInput->CreateTxOuts(this, vout, nCredit)) {
        LogPrintf("%s : failed to get scriptPubKey\n", __func__);
        continue;
      }
      txNew.vout.insert(txNew.vout.end(), vout.begin(), vout.end());

      CAmount nMinFee = 0;
      {
        // Set output amount
        if (txNew.vout.size() == 3) {
          txNew.vout[1].nValue = ((nCredit - nMinFee) / 2 / COINCENT) * COINCENT;
          txNew.vout[2].nValue = nCredit - nMinFee - txNew.vout[1].nValue;
        } else
          txNew.vout[1].nValue = nCredit - nMinFee;
      }

      // Limit size
      uint32_t nBytes = ::GetSerializeSize(txNew);
      if (nBytes >= DEFAULT_BLOCK_MAX_SIZE / 5) return error("CreateCoinStake : exceeded coinstake size limit");

      uint256 hashTxOut = txNew.GetHash();
      CTxIn in;
      if (!stakeInput->CreateTxIn(this, in, hashTxOut)) {
        LogPrintf("%s : failed to create TxIn\n", __func__);
        txNew.vin.clear();
        txNew.vout.clear();
        nCredit = 0;
        continue;
      }
      txNew.vin.emplace_back(in);

      fKernelFound = true;
      break;
    }
    if (fKernelFound) break;  // if kernel is found stop searching
  }
  if (!fKernelFound) return false;

  // Sign for Tessa
  int nIn = 0;
  
    for (CTxIn txIn : txNew.vin) {
      const CWalletTx* wtx = GetWalletTx(txIn.prevout.hash);
      if (!SignSignature(*this, *wtx, txNew, nIn++)) return error("CreateCoinStake : failed to sign coinstake");
    }
  

  // Successfully generated coinstake
  return true;
}

/**
 * Call after CreateTransaction unless you want to abort
 */
bool CWallet::CommitTransaction(CWalletTx& wtxNew, CReserveKey& reservekey, std::string strCommand) {
  {
    LOCK2(cs_main, cs_wallet);
    LogPrintf("CommitTransaction:\n%s", wtxNew.ToString());
    {
      // Take key pair from key pool so it won't be used again
      reservekey.KeepKey();

      // Add tx to wallet, because if it has change it's also ours,
      // otherwise just for transaction history.
      AddToWallet(wtxNew);

      // Notify that old coins are spent
      {
        set<uint256> updated_hahes;
        for (const CTxIn& txin : wtxNew.vin) {
          // notify only once
          if (updated_hahes.find(txin.prevout.hash) != updated_hahes.end()) continue;

          CWalletTx& coin = mapWallet[txin.prevout.hash];
          coin.BindWallet(this);
          NotifyTransactionChanged.fire(this, txin.prevout.hash, CT_UPDATED);
          updated_hahes.insert(txin.prevout.hash);
        }
      }
    }

    // Track how many getdata requests our transaction gets
    mapRequestCount[wtxNew.GetHash()] = 0;

    // Broadcast
    if (!wtxNew.AcceptToMemoryPool(false)) {
      // This must not fail. The transaction has already been signed and recorded.
      LogPrintf("CommitTransaction() : Error: Transaction not valid\n");
      return false;
    }
    wtxNew.RelayWalletTransaction(strCommand);
  }
  return true;
}

bool CWallet::AddAccountingEntry(const CAccountingEntry& acentry) {
  if (!gWalletDB.WriteAccountingEntry_Backend(acentry)) return false;

  laccentries.push_back(acentry);
  CAccountingEntry& entry = laccentries.back();
  wtxOrdered.insert(make_pair(entry.nOrderPos, TxPair((CWalletTx*)nullptr, &entry)));

  return true;
}

CAmount CWallet::GetTotalValue(std::vector<CTxIn> vCoins) {
  CAmount nTotalValue = 0;
  CWalletTx wtx;
  for (CTxIn i : vCoins) {
    if (mapWallet.count(i.prevout.hash)) {
      CWalletTx& wtx = mapWallet[i.prevout.hash];
      if (i.prevout.n < wtx.vout.size()) { nTotalValue += wtx.vout[i.prevout.n].nValue; }
    } else {
      LogPrintf("GetTotalValue -- Couldn't find transaction\n");
    }
  }
  return nTotalValue;
}

DBErrors CWallet::LoadWallet(bool& fFirstRunRet) {
  if (!fFileBacked) return DB_LOAD_OK;
  fFirstRunRet = false;
  DBErrors nLoadWalletRet = gWalletDB.LoadWallet(this);
  if (nLoadWalletRet != DB_LOAD_OK) return nLoadWalletRet;
  fFirstRunRet = !vchDefaultKey.IsValid();
  uiInterface.LoadWallet.fire(this);
  return DB_LOAD_OK;
}

DBErrors CWallet::ZapWalletTx(std::vector<CWalletTx>& vWtx) {
  if (!fFileBacked) return DB_LOAD_OK;
  DBErrors nZapWalletTxRet = gWalletDB.ZapWalletTx(this, vWtx);
  if (nZapWalletTxRet != DB_LOAD_OK) return nZapWalletTxRet;
  return DB_LOAD_OK;
}

bool CWallet::SetAddressBook(const CTxDestination& address, const string& strName, const string& strPurpose) {
  bool fUpdated = false;
  {
    LOCK(cs_wallet);  // mapAddressBook
    auto mi = mapAddressBook.find(address);
    fUpdated = mi != mapAddressBook.end();
    mapAddressBook[address].name = strName;
    if (!strPurpose.empty()) /* update purpose only if requested */
      mapAddressBook[address].purpose = strPurpose;
  }
  NotifyAddressBookChanged.fire(this, address, strName, ::IsMine(*this, address) != ISMINE_NO, strPurpose,
                                (fUpdated ? CT_UPDATED : CT_NEW));
  if (!fFileBacked) return false;
  if (!strPurpose.empty() && !gWalletDB.WritePurpose(EncodeDestination(address), strPurpose)) return false;
  return gWalletDB.WriteName(EncodeDestination(address), strName);
}

bool CWallet::DelAddressBook(const CTxDestination& address) {
  {
    LOCK(cs_wallet);  // mapAddressBook

    if (fFileBacked) {
      // Delete destdata tuples associated with address
      std::string strAddress = EncodeDestination(address);
      for (const auto& item : mapAddressBook[address].destdata) { gWalletDB.EraseDestData(strAddress, item.first); }
    }
    mapAddressBook.erase(address);
  }

  NotifyAddressBookChanged.fire(this, address, "", ::IsMine(*this, address) != ISMINE_NO, "", CT_DELETED);

  if (!fFileBacked) return false;
  gWalletDB.ErasePurpose(EncodeDestination(address));
  return gWalletDB.EraseName(EncodeDestination(address));
}

bool CWallet::SetDefaultKey(const CPubKey& vchPubKey) {
  if (fFileBacked) {
    if (!gWalletDB.WriteDefaultKey(vchPubKey)) return false;
  }
  vchDefaultKey = vchPubKey;
  return true;
}

/**
 * Mark old keypool keys as used,
 * and generate all new keys
 */
bool CWallet::NewKeyPool() {
  {
    LOCK(cs_wallet);
    for (int64_t nIndex : setKeyPool) gWalletDB.ErasePool(nIndex);
    setKeyPool.clear();

    if (IsLocked()) return false;

    int64_t nKeys = max(GetArg("-keypool", KEY_RES_SIZE), (int64_t)0);
    for (int i = 0; i < nKeys; i++) {
      int64_t nIndex = i + 1;
      gWalletDB.WritePool(nIndex, CKeyPool(GenerateNewKey()));
      setKeyPool.insert(nIndex);
    }
    // gWalletDB.TxnCommit();
    LogPrintf("CWallet::NewKeyPool wrote %d new keys\n", nKeys);
  }
  return true;
}

bool CWallet::TopUpKeyPool(uint32_t kpSize) {
  {
    LOCK(cs_wallet);

    if (IsLocked()) return false;

    // Top up key pool
    uint32_t nTargetSize;
    if (kpSize > 0)
      nTargetSize = kpSize;
    else
      nTargetSize = max(GetArg("-keypool", KEY_RES_SIZE), (int64_t)0);

    while (setKeyPool.size() < (nTargetSize + 1)) {
      int64_t nEnd = 1;
      if (!setKeyPool.empty()) nEnd = *(--setKeyPool.end()) + 1;
      if (!gWalletDB.WritePool(nEnd, CKeyPool(GenerateNewKey())))
        throw runtime_error("TopUpKeyPool() : writing generated key failed");
      setKeyPool.insert(nEnd);
      LogPrintf("keypool added key %d, size=%u\n", nEnd, setKeyPool.size());
      double dProgress = 100.f * nEnd / (nTargetSize + 1);
      std::string strMsg = strprintf(_("Loading wallet... (%3.2f %%)"), dProgress);
      uiInterface.InitMessage.fire(strMsg);
    }
    // gWalletDB.TxnCommit();
  }
  return true;
}

void CWallet::ReserveKeyFromKeyPool(int64_t& nIndex, CKeyPool& keypool) {
  nIndex = -1;
  keypool.vchPubKey = CPubKey();
  {
    LOCK(cs_wallet);

    if (!IsLocked()) TopUpKeyPool();

    // Get the oldest key
    if (setKeyPool.empty()) return;

    nIndex = *(setKeyPool.begin());
    setKeyPool.erase(setKeyPool.begin());
    if (!gWalletDB.ReadPool(nIndex, keypool)) throw runtime_error("ReserveKeyFromKeyPool() : read failed");
    if (!HaveKey(keypool.vchPubKey.GetID())) throw runtime_error("ReserveKeyFromKeyPool() : unknown key in key pool");
    assert(keypool.vchPubKey.IsValid());
    // LogPrintf("keypool reserve %d\n", nIndex);
  }
}

void CWallet::KeepKey(int64_t nIndex) {
  // Remove from key pool
  if (fFileBacked) { gWalletDB.ErasePool(nIndex); }
  // LogPrintf("keypool keep %d\n", nIndex);
}

void CWallet::ReturnKey(int64_t nIndex) {
  // Return to key pool
  {
    LOCK(cs_wallet);
    setKeyPool.insert(nIndex);
  }
  // LogPrintf("keypool return %d\n", nIndex);
}

bool CWallet::GetKeyFromPool(CPubKey& result) {
  int64_t nIndex = 0;
  CKeyPool keypool;
  {
    LOCK(cs_wallet);
    ReserveKeyFromKeyPool(nIndex, keypool);
    if (nIndex == -1) {
      if (IsLocked()) return false;
      result = GenerateNewKey();
      return true;
    }
    KeepKey(nIndex);
    result = keypool.vchPubKey;
  }
  return true;
}

int64_t CWallet::GetOldestKeyPoolTime() {
  int64_t nIndex = 0;
  CKeyPool keypool;
  ReserveKeyFromKeyPool(nIndex, keypool);
  if (nIndex == -1) return GetTime();
  ReturnKey(nIndex);
  return keypool.nTime;
}

std::map<CTxDestination, CAmount> CWallet::GetAddressBalances() {
  map<CTxDestination, CAmount> balances;

  {
    LOCK(cs_wallet);
    for (auto walletEntry : mapWallet) {
      CWalletTx* pcoin = &walletEntry.second;

      if (!IsFinalTx(*pcoin) || !pcoin->IsTrusted()) continue;

      if (pcoin->IsCoinBase() && pcoin->GetBlocksToMaturity() > 0) continue;

      int nDepth = pcoin->GetDepthInMainChain();
      if (nDepth < (pcoin->IsFromMe(ISMINE_ALL) ? 0 : 1)) continue;

      for (uint32_t i = 0; i < pcoin->vout.size(); i++) {
        CTxDestination addr;
        if (!IsMine(pcoin->vout[i])) continue;
        if (!ExtractDestination(pcoin->vout[i].scriptPubKey, addr)) continue;

        CAmount n = IsSpent(walletEntry.first, i) ? 0 : pcoin->vout[i].nValue;

        if (!balances.count(addr)) balances[addr] = 0;
        balances[addr] += n;
      }
    }
  }

  return balances;
}

set<set<CTxDestination> > CWallet::GetAddressGroupings() {
  AssertLockHeld(cs_wallet);  // mapWallet
  set<set<CTxDestination> > groupings;
  set<CTxDestination> grouping;

  for (auto walletEntry : mapWallet) {
    CWalletTx* pcoin = &walletEntry.second;

    if (pcoin->vin.size() > 0) {
      bool any_mine = false;
      // group all input addresses with each other
      for (CTxIn txin : pcoin->vin) {
        CTxDestination address;
        if (!IsMine(txin)) /* If this input isn't mine, ignore it */
          continue;
        if (!ExtractDestination(mapWallet[txin.prevout.hash].vout[txin.prevout.n].scriptPubKey, address)) continue;
        grouping.insert(address);
        any_mine = true;
      }

      // group change with input addresses
      if (any_mine) {
        for (CTxOut txout : pcoin->vout)
          if (IsChange(txout)) {
            CTxDestination txoutAddr;
            if (!ExtractDestination(txout.scriptPubKey, txoutAddr)) continue;
            grouping.insert(txoutAddr);
          }
      }
      if (grouping.size() > 0) {
        groupings.insert(grouping);
        grouping.clear();
      }
    }

    // group lone addrs by themselves
    for (uint32_t i = 0; i < pcoin->vout.size(); i++)
      if (IsMine(pcoin->vout[i])) {
        CTxDestination address;
        if (!ExtractDestination(pcoin->vout[i].scriptPubKey, address)) continue;
        grouping.insert(address);
        groupings.insert(grouping);
        grouping.clear();
      }
  }

  set<set<CTxDestination>*> uniqueGroupings;         // a set of pointers to groups of addresses
  map<CTxDestination, set<CTxDestination>*> setmap;  // map addresses to the unique group containing it
  for (set<CTxDestination> grouping : groupings) {
    // make a set of all the groups hit by this new group
    set<set<CTxDestination>*> hits;
    map<CTxDestination, set<CTxDestination>*>::iterator it;
    for (auto& address : grouping)
      if ((it = setmap.find(address)) != setmap.end()) hits.insert((*it).second);

    // merge all hit groups into a new single group and delete old groups
    set<CTxDestination>* merged = new set<CTxDestination>(grouping);
    for (set<CTxDestination>* hit : hits) {
      merged->insert(hit->begin(), hit->end());
      uniqueGroupings.erase(hit);
      delete hit;
    }
    uniqueGroupings.insert(merged);

    // update setmap
    for (auto& element : *merged) setmap[element] = merged;
  }

  set<set<CTxDestination> > ret;
  for (set<CTxDestination>* uniqueGrouping : uniqueGroupings) {
    ret.insert(*uniqueGrouping);
    delete uniqueGrouping;
  }

  return ret;
}

set<CTxDestination> CWallet::GetAccountAddresses(const string& strAccount) const {
  LOCK(cs_wallet);
  set<CTxDestination> result;
  for (const auto& item : mapAddressBook) {
    const CTxDestination& address = item.first;
    const string& strName = item.second.name;
    if (strName == strAccount) result.insert(address);
  }
  return result;
}

bool CReserveKey::GetReservedKey(CPubKey& pubkey) {
  if (nIndex == -1) {
    CKeyPool keypool;
    pwallet->ReserveKeyFromKeyPool(nIndex, keypool);
    if (nIndex != -1)
      vchPubKey = keypool.vchPubKey;
    else {
      return false;
    }
  }
  assert(vchPubKey.IsValid());
  pubkey = vchPubKey;
  return true;
}

void CReserveKey::KeepKey() {
  if (nIndex != -1) pwallet->KeepKey(nIndex);
  nIndex = -1;
  vchPubKey = CPubKey();
}

void CReserveKey::ReturnKey() {
  if (nIndex != -1) pwallet->ReturnKey(nIndex);
  nIndex = -1;
  vchPubKey = CPubKey();
}

void CWallet::GetAllReserveKeys(set<CKeyID>& setAddress) const {
  setAddress.clear();

  LOCK2(cs_main, cs_wallet);
  for (const int64_t& id : setKeyPool) {
    CKeyPool keypool;
    if (!gWalletDB.ReadPool(id, keypool)) throw runtime_error("GetAllReserveKeyHashes() : read failed");
    assert(keypool.vchPubKey.IsValid());
    CKeyID keyID = keypool.vchPubKey.GetID();
    if (!HaveKey(keyID)) throw runtime_error("GetAllReserveKeyHashes() : unknown key in key pool");
    setAddress.insert(keyID);
  }
}

bool CWallet::UpdatedTransaction(const uint256& hashTx) {
  {
    LOCK(cs_wallet);
    // Only notify UI if this transaction is in this wallet
    const auto mi = mapWallet.find(hashTx);
    if (mi != mapWallet.end()) {
      NotifyTransactionChanged.fire(this, hashTx, CT_UPDATED);
      return true;
    }
  }
  return false;
}

void CWallet::LockCoin(COutPoint& output) {
  AssertLockHeld(cs_wallet);  // setLockedCoins
  setLockedCoins.insert(output);
}

void CWallet::UnlockCoin(COutPoint& output) {
  AssertLockHeld(cs_wallet);  // setLockedCoins
  setLockedCoins.erase(output);
}

void CWallet::UnlockAllCoins() {
  AssertLockHeld(cs_wallet);  // setLockedCoins
  setLockedCoins.clear();
}

bool CWallet::IsLockedCoin(uint256 hash, uint32_t n) const {
  AssertLockHeld(cs_wallet);  // setLockedCoins
  COutPoint outpt(hash, n);

  return (setLockedCoins.count(outpt) > 0);
}

void CWallet::ListLockedCoins(std::vector<COutPoint>& vOutpts) {
  AssertLockHeld(cs_wallet);  // setLockedCoins
  for (auto& it : setLockedCoins) {
    COutPoint outpt = it;
    vOutpts.push_back(outpt);
  }
}

/** @} */  // end of Actions

class CAffectedKeysVisitor {
 private:
  const CKeyStore& keystore;
  std::vector<CKeyID>& vKeys;

 public:
  CAffectedKeysVisitor(const CKeyStore& keystoreIn, std::vector<CKeyID>& vKeysIn)
      : keystore(keystoreIn), vKeys(vKeysIn) {}

  void Process(const CScript& script) {
    txnouttype type;
    std::vector<CTxDestination> vDest;
    int nRequired;
    if (ExtractDestinations(script, type, vDest, nRequired)) {
      for (const CTxDestination& dest : vDest) std::visit(*this, dest);
    }
  }

  void operator()(const CKeyID& keyId) {
    if (keystore.HaveKey(keyId)) vKeys.push_back(keyId);
  }

  void operator()(const CScriptID& scriptId) {
    CScript script;
    if (keystore.GetCScript(scriptId, script)) Process(script);
  }

  void operator()(const CNoDestination& none) {}
};

void CWallet::GetKeyBirthTimes(std::map<CKeyID, int64_t>& mapKeyBirth) const {
  AssertLockHeld(cs_wallet);  // mapKeyMetadata
  mapKeyBirth.clear();

  // get birth times for keys with metadata
  for (auto& it : mapKeyMetadata)
    if (it.second.nCreateTime) mapKeyBirth[it.first] = it.second.nCreateTime;

  // map in which we'll infer heights of other keys
  CBlockIndex* pindexMax = chainActive[std::max(
      0, chainActive.Height() - 144)];  // the tip can be reorganised; use a 144-block safety margin
  std::map<CKeyID, CBlockIndex*> mapKeyFirstBlock;
  std::set<CKeyID> setKeys;
  GetKeys(setKeys);
  for (const CKeyID& keyid : setKeys) {
    if (mapKeyBirth.count(keyid) == 0) mapKeyFirstBlock[keyid] = pindexMax;
  }
  setKeys.clear();

  // if there are no such keys, we're done
  if (mapKeyFirstBlock.empty()) return;

  // find first block that affects those keys, if there are any left
  std::vector<CKeyID> vAffected;
  for (auto& it : mapWallet) {
    // iterate over all wallet transactions...
    const CWalletTx& wtx = it.second;
    const auto blit = mapBlockIndex.find(wtx.hashBlock);
    if (blit != mapBlockIndex.end() && chainActive.Contains(blit->second)) {
      // ... which are already in a block
      int nHeight = blit->second->nHeight;
      for (const CTxOut& txout : wtx.vout) {
        // iterate over all their outputs
        CAffectedKeysVisitor(*this, vAffected).Process(txout.scriptPubKey);
        for (const CKeyID& keyid : vAffected) {
          // ... and all their affected keys
          auto rit = mapKeyFirstBlock.find(keyid);
          if (rit != mapKeyFirstBlock.end() && nHeight < rit->second->nHeight) rit->second = blit->second;
        }
        vAffected.clear();
      }
    }
  }

  // Extract block timestamps for those keys
  for (auto& it : mapKeyFirstBlock)
    mapKeyBirth[it.first] = it.second->GetBlockTime() - 7200;  // block times can be 2h off
}

uint32_t CWallet::ComputeTimeSmart(const CWalletTx& wtx) const {
  uint32_t nTimeSmart = wtx.nTimeReceived;
  if (!wtx.hashBlock.IsNull()) {
    if (mapBlockIndex.count(wtx.hashBlock)) {
      int64_t latestNow = wtx.nTimeReceived;
      int64_t latestEntry = 0;
      {
        // Tolerate times up to the last timestamp in the wallet not more than 5 minutes into the future
        int64_t latestTolerated = latestNow + 300;
        TxItems txOrdered = wtxOrdered;
        for (const auto& it : reverse_iterate(txOrdered)) {
          CWalletTx* const pwtx = it.second.first;
          if (pwtx == &wtx) continue;
          CAccountingEntry* const pacentry = it.second.second;
          int64_t nSmartTime;
          if (pwtx) {
            nSmartTime = pwtx->nTimeSmart;
            if (!nSmartTime) nSmartTime = pwtx->nTimeReceived;
          } else
            nSmartTime = pacentry->nTime;
          if (nSmartTime <= latestTolerated) {
            latestEntry = nSmartTime;
            if (nSmartTime > latestNow) latestNow = nSmartTime;
            break;
          }
        }
      }

      int64_t blocktime = mapBlockIndex[wtx.hashBlock]->GetBlockTime();
      nTimeSmart = std::max(latestEntry, std::min(blocktime, latestNow));
    } else
      LogPrintf("AddToWallet() : found %s in block %s not in index\n", wtx.GetHash().ToString(),
                wtx.hashBlock.ToString());
  }
  return nTimeSmart;
}

bool CWallet::AddDestData(const CTxDestination& dest, const std::string& key, const std::string& value) {
  try {
    std::get<CNoDestination>(dest);
  } catch (std::bad_variant_access&) { return false; }
  mapAddressBook[dest].destdata.insert(std::make_pair(key, value));
  if (!fFileBacked) return true;
  return gWalletDB.WriteDestData(EncodeDestination(dest), key, value);
}

bool CWallet::EraseDestData(const CTxDestination& dest, const std::string& key) {
  if (!mapAddressBook[dest].destdata.erase(key)) return false;
  if (!fFileBacked) return true;
  return gWalletDB.EraseDestData(EncodeDestination(dest), key);
}

bool CWallet::LoadDestData(const CTxDestination& dest, const std::string& key, const std::string& value) {
  mapAddressBook[dest].destdata.insert(std::make_pair(key, value));
  return true;
}

bool CWallet::GetDestData(const CTxDestination& dest, const std::string& key, std::string* value) const {
  std::map<CTxDestination, CAddressBookData>::const_iterator i = mapAddressBook.find(dest);
  if (i != mapAddressBook.end()) {
    const auto j = i->second.destdata.find(key);
    if (j != i->second.destdata.end()) {
      if (value) *value = j->second;
      return true;
    }
  }
  return false;
}

void CWallet::AutoCombineDust() {
  LOCK2(cs_main, cs_wallet);
  if (chainActive.Tip()->nTime < (GetAdjustedTime() - 300) || IsLocked()) { return; }

  map<CTxDestination, vector<COutput> > mapCoinsByAddress = AvailableCoinsByAddress(true, nAutoCombineThreshold * COIN);

  // coins are sectioned by address. This combination code only wants to combine inputs that belong to the same address
  for (auto& it : mapCoinsByAddress) {
    vector<COutput> vCoins, vRewardCoins;
    vCoins = it.second;

    // We don't want the tx to be refused for being too large
    // we use 50 bytes as a base tx size (2 output: 2*34 + overhead: 10 -> 90 to be certain)
    uint32_t txSizeEstimate = 90;

    // find rewards that need to be combined
    CCoinControl* coinControl = new CCoinControl();
    CAmount nTotalRewardsValue = 0;
    for (const COutput& out : vCoins) {
      if (!out.fSpendable) continue;
      // no coins should get this far if they dont have proper maturity, this is double checking
      if (out.tx->IsCoinStake() && out.tx->GetDepthInMainChain() < COINBASE_MATURITY + 1) continue;

      COutPoint outpt(out.tx->GetHash(), out.i);
      coinControl->Select(outpt);
      vRewardCoins.push_back(out);
      nTotalRewardsValue += out.Value();

      // Combine to the threshold and not way above
      if (nTotalRewardsValue > nAutoCombineThreshold * COIN) break;

      // Around 180 bytes per input. We use 190 to be certain
      txSizeEstimate += 190;
      if (txSizeEstimate >= MAX_STANDARD_TX_SIZE - 200) break;
    }

    // if no inputs found then return
    if (!coinControl->HasSelected()) continue;

    // we cannot combine one coin with itself
    if (vRewardCoins.size() <= 1) continue;

    vector<pair<CScript, CAmount> > vecSend;
    CScript scriptPubKey = GetScriptForDestination(it.first);
    vecSend.push_back(make_pair(scriptPubKey, nTotalRewardsValue));

    // Send change to same address
    CTxDestination destMyAddress;
    if (!ExtractDestination(scriptPubKey, destMyAddress)) {
      LogPrintf("AutoCombineDust: failed to extract destination\n");
      continue;
    }
    coinControl->destChange = destMyAddress;

    // Create the transaction and commit it to the network
    CWalletTx wtx;
    CReserveKey keyChange(
        this);  // this change address does not end up being used, because change is returned with coin control switch
    string strErr;
    CAmount nFeeRet = 0;

    // 10% safety margin to avoid "Insufficient funds" errors
    vecSend[0].second = nTotalRewardsValue - (nTotalRewardsValue / 10);

    if (!CreateTransaction(vecSend, wtx, keyChange, nFeeRet, strErr, coinControl, ALL_COINS, false, CAmount(0))) {
      LogPrintf("AutoCombineDust createtransaction failed, reason: %s\n", strErr);
      continue;
    }

    // we don't combine below the threshold unless the fees are 0 to avoid paying fees over fees over fees
    if (nTotalRewardsValue < nAutoCombineThreshold * COIN && nFeeRet > 0) continue;

    if (!CommitTransaction(wtx, keyChange)) {
      LogPrintf("AutoCombineDust transaction commit failed\n");
      continue;
    }

    LogPrintf("AutoCombineDust sent transaction\n");

    delete coinControl;
  }
}

bool CWallet::MultiSend() {
  LOCK2(cs_main, cs_wallet);
  // Stop the old blocks from sending multisends
  if (chainActive.Tip()->nTime < (GetAdjustedTime() - 300) || IsLocked()) { return false; }

  if (chainActive.Tip()->nHeight <= nLastMultiSendHeight) {
    LogPrintf("Multisend: lastmultisendheight is higher than current best height\n");
    return false;
  }

  std::vector<COutput> vCoins;
  AvailableCoins(vCoins);
  bool stakeSent = false;
  bool mnSent = false;
  for (const COutput& out : vCoins) {
    // need output with precise confirm count - this is how we identify which is the output to send
    if (out.tx->GetDepthInMainChain() != Params().COINBASE_MATURITY() + 1) continue;

    COutPoint outpoint(out.tx->GetHash(), out.i);
    bool sendMSonMNReward = false;
    bool sendMSOnStake = fMultiSendStake && out.tx->IsCoinStake() &&
                         !sendMSonMNReward;  // output is either mnreward or stake reward, not both

    if (!(sendMSOnStake || sendMSonMNReward)) continue;

    CTxDestination destMyAddress;
    if (!ExtractDestination(out.tx->vout[out.i].scriptPubKey, destMyAddress)) {
      LogPrintf("Multisend: failed to extract destination\n");
      continue;
    }

    // Disabled Addresses won't send MultiSend transactions
    if (vDisabledAddresses.size() > 0) {
      for (uint32_t i = 0; i < vDisabledAddresses.size(); i++) {
        if (vDisabledAddresses[i] == EncodeDestination(destMyAddress)) {
          LogPrintf("Multisend: disabled address preventing multisend\n");
          return false;
        }
      }
    }

    // create new coin control, populate it with the selected utxo, create sending vector
    CCoinControl cControl;
    COutPoint outpt(out.tx->GetHash(), out.i);
    cControl.Select(outpt);
    cControl.destChange = destMyAddress;

    CWalletTx wtx;
    CReserveKey keyChange(
        this);  // this change address does not end up being used, because change is returned with coin control switch
    CAmount nFeeRet = 0;
    vector<pair<CScript, CAmount> > vecSend;

    // loop through multisend vector and add amounts and addresses to the sending vector
    const isminefilter filter = ISMINE_SPENDABLE;
    CAmount nAmount = 0;
    for (uint32_t i = 0; i < vMultiSend.size(); i++) {
      // MultiSend vector is a pair of 1)Address as a std::string 2) Percent of stake to send as an int
      nAmount = ((out.tx->GetCredit(filter) - out.tx->GetDebit(filter)) * vMultiSend[i].second) / 100;
      CTxDestination strAddSend = DecodeDestination(vMultiSend[i].first);
      CScript scriptPubKey;
      scriptPubKey = GetScriptForDestination(strAddSend);
      vecSend.push_back(make_pair(scriptPubKey, nAmount));
    }

    // get the fee amount
    CWalletTx wtxdummy;
    string strErr;
    CreateTransaction(vecSend, wtxdummy, keyChange, nFeeRet, strErr, &cControl, ALL_COINS, false, CAmount(0));
    CAmount nLastSendAmount = vecSend[vecSend.size() - 1].second;
    if (nLastSendAmount < nFeeRet + 500) {
      LogPrintf("%s: fee of %d is too large to insert into last output\n", __func__, nFeeRet + 500);
      return false;
    }
    vecSend[vecSend.size() - 1].second = nLastSendAmount - nFeeRet - 500;

    // Create the transaction and commit it to the network
    if (!CreateTransaction(vecSend, wtx, keyChange, nFeeRet, strErr, &cControl, ALL_COINS, false, CAmount(0))) {
      LogPrintf("MultiSend createtransaction failed\n");
      return false;
    }

    if (!CommitTransaction(wtx, keyChange)) {
      LogPrintf("MultiSend transaction commit failed\n");
      return false;
    } else
      fMultiSendNotify = true;

    // write nLastMultiSendHeight to DB
    nLastMultiSendHeight = chainActive.Tip()->nHeight;
    if (!gWalletDB.WriteMSettings(fMultiSendStake, false, nLastMultiSendHeight))
      LogPrintf("Failed to write MultiSend setting to DB\n");

    LogPrintf("MultiSend successfully sent\n");

    // set which MultiSend triggered
    if (sendMSOnStake)
      stakeSent = true;
    else
      mnSent = true;

    // stop iterating if we have sent out all the MultiSend(s)
    if ((stakeSent && mnSent) || (stakeSent) || (mnSent && !fMultiSendStake)) return true;
  }

  return true;
}

CKeyPool::CKeyPool() { nTime = GetTime(); }

CKeyPool::CKeyPool(const CPubKey& vchPubKeyIn) {
  nTime = GetTime();
  vchPubKey = vchPubKeyIn;
}

int CMerkleTx::SetMerkleBranch(const CBlock& block) {
  AssertLockHeld(cs_main);
  CBlock blockTmp;

  // Update the tx's hashBlock
  hashBlock = block.GetHash();

  // Locate the transaction
  for (nIndex = 0; nIndex < (int)block.vtx.size(); nIndex++)
    if (block.vtx[nIndex] == *(CTransaction*)this) break;
  if (nIndex == (int)block.vtx.size()) {
    vMerkleBranch.clear();
    nIndex = -1;
    LogPrintf("ERROR: SetMerkleBranch() : couldn't find tx in block\n");
    return 0;
  }

  // Fill in merkle branch
  vMerkleBranch = block.GetMerkleBranch(nIndex);

  // Is the tx in a block that's in the main chain
  auto mi = mapBlockIndex.find(hashBlock);
  if (mi == mapBlockIndex.end()) return 0;
  const CBlockIndex* pindex = (*mi).second;
  if (!pindex || !chainActive.Contains(pindex)) return 0;

  return chainActive.Height() - pindex->nHeight + 1;
}

int CMerkleTx::GetDepthInMainChainINTERNAL(const CBlockIndex*& pindexRet) const {
  if (hashBlock.IsNull() || nIndex == -1) return 0;
  AssertLockHeld(cs_main);

  // Find the block it claims to be in
  auto mi = mapBlockIndex.find(hashBlock);
  if (mi == mapBlockIndex.end()) return 0;
  CBlockIndex* pindex = (*mi).second;
  if (!pindex || !chainActive.Contains(pindex)) return 0;

  // Make sure the merkle branch connects to this block
  if (!fMerkleVerified) {
    if (CBlock::CheckMerkleBranch(GetHash(), vMerkleBranch, nIndex) != pindex->hashMerkleRoot) return 0;
    fMerkleVerified = true;
  }

  pindexRet = pindex;
  return chainActive.Height() - pindex->nHeight + 1;
}

int CMerkleTx::GetDepthInMainChain(const CBlockIndex*& pindexRet, bool enableIX) const {
  AssertLockHeld(cs_main);
  int nResult = GetDepthInMainChainINTERNAL(pindexRet);
  if (nResult == 0 && !mempool.exists(GetHash())) return -1;  // Not in chain, not in mempool
  return nResult;
}

int CMerkleTx::GetBlocksToMaturity() const {
  LOCK(cs_main);
  if (!(IsCoinBase() || IsCoinStake())) return 0;
  return max(0, (Params().COINBASE_MATURITY() + 1) - GetDepthInMainChain());
}

bool CMerkleTx::AcceptToMemoryPool(bool fLimitFree, bool fRejectInsaneFee, bool ignoreFees) {
  CValidationState state;
  bool fAccepted = ::AcceptToMemoryPool(mempool, state, *this, fLimitFree, nullptr, fRejectInsaneFee, ignoreFees);
  if (!fAccepted) LogPrintf("%s : %s\n", __func__, state.GetRejectReason());
  return fAccepted;
}

int CMerkleTx::GetTransactionLockSignatures() const {
  if (CheckLargeWork()) return -2;
  return -1;
}

bool CMerkleTx::IsTransactionLockTimedOut() const { return false; }

// Given a set of inputs, find the public key that contributes the most coins to the input set
CScript GetLargestContributor(set<pair<const CWalletTx*, uint32_t> >& setCoins) {
  map<CScript, CAmount> mapScriptsOut;
  for (const std::pair<const CWalletTx*, uint32_t>& coin : setCoins) {
    CTxOut out = coin.first->vout[coin.second];
    mapScriptsOut[out.scriptPubKey] += out.nValue;
  }

  CScript scriptLargest;
  CAmount nLargestContributor = 0;
  for (auto it : mapScriptsOut) {
    if (it.second > nLargestContributor) {
      scriptLargest = it.first;
      nLargestContributor = it.second;
    }
  }

  return scriptLargest;
}

//----- HD Stuff ------------

CPubKey CWallet::GenerateNewHDMasterKey() {
  CKey key;
  key.MakeNewKey();

  int64_t nCreationTime = GetTime();
  CKeyMetadata metadata(nCreationTime);

  // Calculate the pubkey.
  CPubKey pubkey = key.GetPubKey();
  assert(key.VerifyPubKey(pubkey));

  // Set the hd keypath to "m" -> Master, refers the masterkeyid to itself.
  metadata.hdKeypath = "m";
  metadata.hdMasterKeyID = pubkey.GetID();

  LOCK(cs_wallet);

  // mem store the metadata
  mapKeyMetadata[pubkey.GetID()] = metadata;

  // Write the key&metadata to the database.
  if (!AddKeyPubKey(key, pubkey)) { throw std::runtime_error(std::string(__func__) + ": AddKeyPubKey failed"); }

  return pubkey;
}

bool CWallet::SetHDMasterKeyFromSeed(const uint256 seed) {
  CKey key;

  int64_t nCreationTime = GetTime();
  CKeyMetadata metadata(nCreationTime);

  // Calculate the pubkey.
  CPubKey pubkey = key.GetPubKey();
  assert(key.VerifyPubKey(pubkey));

  // Set the hd keypath to "m" -> Master, refers the masterkeyid to itself.
  metadata.hdKeypath = "m";
  metadata.hdMasterKeyID = pubkey.GetID();

  LOCK(cs_wallet);

  // mem store the metadata
  mapKeyMetadata[pubkey.GetID()] = metadata;

  return SetHDMasterKey(pubkey);
}

bool CWallet::SetHDMasterKey(const CPubKey& pubkey) {
  LOCK(cs_wallet);

  // Store the keyid (hash160) together with the child index counter in the
  // database as a hdchain object.
  CHDChain newHdChain;
  newHdChain.masterKeyID = pubkey.GetID();
  SetHDChain(newHdChain, false);
  return true;
}

bool CWallet::SetHDChain(const CHDChain& chain, bool memonly) {
  LOCK(cs_wallet);
  if (!memonly && !gWalletDB.WriteHDChain(chain)) {
    throw std::runtime_error(std::string(__func__) + ": writing chain failed");
  }

  hdChain = chain;
  return true;
}

bool CWallet::IsHDEnabled() { return !hdChain.masterKeyID.IsNull(); }
