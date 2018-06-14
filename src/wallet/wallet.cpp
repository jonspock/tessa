// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The ClubChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "wallet.h"
#include "init.h"
#include "walletkey.h"
#include "wallettx.h"

#include "accumulators.h"
#include "base58.h"
#include "checkpoints.h"
#include "coincontrol.h"
#include "fs.h"
#include "kernel.h"
#include "net.h"
#include "primitives/transaction.h"
#include "script/script.h"
#include "script/sign.h"
#include "spork.h"
#include "stakeinput.h"
#include "timedata.h"
#include "txdb.h"
#include "util.h"
#include "utilmoneystr.h"
#include "zerochain.h"

#include "denomination_functions.h"
#include "libzerocoin/CoinSpend.h"
#include "libzerocoin/Denominations.h"
#include "libzerocoin/PrivateCoin.h"
#include "libzerocoin/PublicCoin.h"
#include "primitives/deterministicmint.h"
#include "zerowallet.h"
#include <cassert>

#include <boost/algorithm/string/replace.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/thread.hpp>

#define KEY_RES_SIZE 100

using namespace std;

// Only used in this file
enum ZerocoinSpendStatus {
  ZKP_SPEND_OKAY = 0,              // No error
  ZKP_SPEND_ERROR = 1,             // Unspecified class of errors, more details are (hopefully) in the returning text
  ZKP_WALLET_LOCKED = 2,           // Wallet was locked
  ZKP_COMMIT_FAILED = 3,           // Commit failed, reset status
  ZKP_ERASE_SPENDS_FAILED = 4,     // Erasing spends during reset failed
  ZKP_ERASE_NEW_MINTS_FAILED = 5,  // Erasing new mints during reset failed
  ZKP_TRX_FUNDS_PROBLEMS = 6,      // Everything related to available funds
  ZKP_TRX_CREATE = 7,              // Everything related to create the transaction
  ZKP_TRX_CHANGE = 8,              // Everything related to transaction change
  ZKP_TXMINT_GENERAL = 9,          // General errors in MintToTxIn
  ZKP_INVALID_COIN = 10,           // Selected mint coin is not valid
  ZKP_FAILED_ACCUMULATOR_INITIALIZATION = 11,  // Failed to initialize witness
  ZKP_INVALID_WITNESS = 12,                    // Spend coin transaction did not verify
  ZKP_BAD_SERIALIZATION = 13,                  // Transaction verification failed
  ZKP_SPENT_USED_ZKP = 14,                     // Coin has already been spend
  ZKP_TX_TOO_LARGE = 15                        // The transaction is larger than the max tx size
};

/**
 * Settings
 */
CFeeRate payTxFee(DEFAULT_TRANSACTION_FEE);
CAmount maxTxFee = DEFAULT_TRANSACTION_MAXFEE;
unsigned int nTxConfirmTarget = 1;
bool bSpendZeroConfChange = true;
bool bdisableSystemnotifications =
    false;  // Those bubbles can be annoying and slow down the UI when you get lots of trx
bool fSendFreeTransactions = false;
bool fPayAtLeastCustomFee = true;
int64_t nStartupTime = GetTime();  //!< Client startup time for use with automint

/**
 * Fees smaller than this (in u) are considered zero fee (for transaction creation)
 * We are ~100 times smaller then bitcoin now (2015-06-23), set minTxFee 10 times higher
 * so it's still 10 times lower comparing to bitcoin.
 * Override with -mintxfee
 */
CFeeRate CWallet::minTxFee = CFeeRate(10000);

/** @defgroup mapWallet
 *
 * @{
 */

struct CompareValueOnly {
  bool operator()(const pair<CAmount, pair<const CWalletTx*, unsigned int> >& t1,
                  const pair<CAmount, pair<const CWalletTx*, unsigned int> >& t2) const {
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
  AssertLockHeld(cs_wallet);  // mapKeyMetadata
  bool fCompressed = true;    // default to compressed public keys

  RandAddSeedPerfmon();
  CKey secret;
  secret.MakeNewKey(fCompressed);

  // Compressed public keys were introduced in version 0.6.0
  CPubKey pubkey = secret.GetPubKey();
  assert(secret.VerifyPubKey(pubkey));

  // Create new metadata
  int64_t nCreationTime = GetTime();
  mapKeyMetadata[pubkey.GetID()] = CKeyMetadata(nCreationTime);
  if (!nTimeFirstKey || nCreationTime < nTimeFirstKey) nTimeFirstKey = nCreationTime;

  if (!AddKeyPubKey(secret, pubkey)) throw std::runtime_error("CWallet::GenerateNewKey() : AddKey failed");
  return pubkey;
}

bool CWallet::AddKeyPubKey(const CKey& secret, const CPubKey& pubkey) {
  AssertLockHeld(cs_wallet);  // mapKeyMetadata
  if (!CCryptoKeyStore::AddKeyPubKey(secret, pubkey)) return false;

  // check if we need to remove from watch-only
  CScript script;
  script = GetScriptForDestination(pubkey.GetID());
  if (HaveWatchOnly(script)) RemoveWatchOnly(script);

  if (!fFileBacked) return true;
  if (!IsCrypted()) {
    return CWalletDB(strWalletFile).WriteKey(pubkey, secret.GetPrivKey(), mapKeyMetadata[pubkey.GetID()]);
  }
  return true;
}

bool CWallet::AddCryptedKey(const CPubKey& vchPubKey, const vector<unsigned char>& vchCryptedSecret) {
  if (!CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret)) return false;
  if (!fFileBacked) return true;
  {
    LOCK(cs_wallet);
    if (pwalletdbEncryption)
      return pwalletdbEncryption->WriteCryptedKey(vchPubKey, vchCryptedSecret, mapKeyMetadata[vchPubKey.GetID()]);
    else
      return CWalletDB(strWalletFile).WriteCryptedKey(vchPubKey, vchCryptedSecret, mapKeyMetadata[vchPubKey.GetID()]);
  }
  return false;
}

bool CWallet::LoadKeyMetadata(const CPubKey& pubkey, const CKeyMetadata& meta) {
  AssertLockHeld(cs_wallet);  // mapKeyMetadata
  if (meta.nCreateTime && (!nTimeFirstKey || meta.nCreateTime < nTimeFirstKey)) nTimeFirstKey = meta.nCreateTime;

  mapKeyMetadata[pubkey.GetID()] = meta;
  return true;
}

bool CWallet::LoadCryptedKey(const CPubKey& vchPubKey, const std::vector<unsigned char>& vchCryptedSecret) {
  return CCryptoKeyStore::AddCryptedKey(vchPubKey, vchCryptedSecret);
}

bool CWallet::AddCScript(const CScript& redeemScript) {
  if (!CCryptoKeyStore::AddCScript(redeemScript)) return false;
  if (!fFileBacked) return true;
  return CWalletDB(strWalletFile).WriteCScript(Hash160(redeemScript), redeemScript);
}

bool CWallet::LoadCScript(const CScript& redeemScript) {
  /* A sanity check was added in pull #3843 to avoid adding redeemScripts
   * that never can be redeemed. However, old wallets may still contain
   * these. Do not add them to the wallet and warn. */
  if (redeemScript.size() > MAX_SCRIPT_ELEMENT_SIZE) {
    std::string strAddr = CBitcoinAddress(CScriptID(redeemScript)).ToString();
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
  NotifyWatchonlyChanged(true);
  if (!fFileBacked) return true;
  return CWalletDB(strWalletFile).WriteWatchOnly(dest);
}

bool CWallet::RemoveWatchOnly(const CScript& dest) {
  AssertLockHeld(cs_wallet);
  if (!CCryptoKeyStore::RemoveWatchOnly(dest)) return false;
  if (!HaveWatchOnly()) NotifyWatchonlyChanged(false);
  if (fFileBacked)
    if (!CWalletDB(strWalletFile).EraseWatchOnly(dest)) return false;

  return true;
}

bool CWallet::LoadWatchOnly(const CScript& dest) { return CCryptoKeyStore::AddWatchOnly(dest); }

bool CWallet::AddMultiSig(const CScript& dest) {
  if (!CCryptoKeyStore::AddMultiSig(dest)) return false;
  nTimeFirstKey = 1;  // No birthday information
  NotifyMultiSigChanged(true);
  if (!fFileBacked) return true;
  return CWalletDB(strWalletFile).WriteMultiSig(dest);
}

bool CWallet::RemoveMultiSig(const CScript& dest) {
  AssertLockHeld(cs_wallet);
  if (!CCryptoKeyStore::RemoveMultiSig(dest)) return false;
  if (!HaveMultiSig()) NotifyMultiSigChanged(false);
  if (fFileBacked)
    if (!CWalletDB(strWalletFile).EraseMultiSig(dest)) return false;

  return true;
}

bool CWallet::LoadMultiSig(const CScript& dest) { return CCryptoKeyStore::AddMultiSig(dest); }

bool CWallet::Unlock(const SecureString& strWalletPassphrase, bool anonymizeOnly) {
  SecureString strWalletPassphraseFinal;

  if (!IsLocked()) {
    fWalletUnlockAnonymizeOnly = anonymizeOnly;
    return true;
  }

  strWalletPassphraseFinal = strWalletPassphrase;

  CCrypter crypter;
  CKeyingMaterial vMasterKey;

  {
    LOCK(cs_wallet);
    for (const MasterKeyMap::value_type& pMasterKey : mapMasterKeys) {
      if (!crypter.SetKeyFromPassphrase(strWalletPassphraseFinal, pMasterKey.second.vchSalt,
                                        pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
        return false;
      if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vMasterKey)) continue;  // try another master key
      if (CCryptoKeyStore::Unlock(vMasterKey)) {
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
    CKeyingMaterial vMasterKey;
    for (MasterKeyMap::value_type& pMasterKey : mapMasterKeys) {
      if (!crypter.SetKeyFromPassphrase(strOldWalletPassphraseFinal, pMasterKey.second.vchSalt,
                                        pMasterKey.second.nDeriveIterations, pMasterKey.second.nDerivationMethod))
        return false;
      if (!crypter.Decrypt(pMasterKey.second.vchCryptedKey, vMasterKey)) return false;
      if (CCryptoKeyStore::Unlock(vMasterKey)) {
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
        if (!crypter.Encrypt(vMasterKey, pMasterKey.second.vchCryptedKey)) return false;
        CWalletDB(strWalletFile).WriteMasterKey(pMasterKey.first, pMasterKey.second);
        if (fWasLocked) Lock();

        return true;
      }
    }
  }

  return false;
}

void CWallet::SetBestChain(const CBlockLocator& loc) {
  CWalletDB walletdb(strWalletFile);
  walletdb.WriteBestBlock(loc);
}

bool CWallet::SetMinVersion(enum WalletFeature nVersion, CWalletDB* pwalletdbIn, bool fExplicit) {
  LOCK(cs_wallet);  // nWalletVersion
  if (nWalletVersion >= nVersion) return true;

  // when doing an explicit upgrade, if we pass the max version permitted, upgrade all the way
  if (fExplicit && nVersion > nWalletMaxVersion) nVersion = FEATURE_LATEST;

  nWalletVersion = nVersion;

  if (nVersion > nWalletMaxVersion) nWalletMaxVersion = nVersion;

  if (fFileBacked) {
    CWalletDB* pwalletdb = pwalletdbIn ? pwalletdbIn : new CWalletDB(strWalletFile);
    pwalletdb->WriteMinVersion(nWalletVersion);
    if (!pwalletdbIn) delete pwalletdb;
  }

  return true;
}

bool CWallet::SetMaxVersion(int nVersion) {
  LOCK(cs_wallet);  // nWalletVersion, nWalletMaxVersion
  // cannot downgrade below current version
  if (nWalletVersion > nVersion) return false;

  nWalletMaxVersion = nVersion;

  return true;
}

set<uint256> CWallet::GetConflicts(const uint256& txid) const {
  set<uint256> result;
  AssertLockHeld(cs_wallet);

  const auto it = mapWallet.find(txid);
  if (it == mapWallet.end()) return result;
  const CWalletTx& wtx = it->second;

  std::pair<TxSpends::const_iterator, TxSpends::const_iterator> range;

  for (const CTxIn& txin : wtx.vin) {
    if (mapTxSpends.count(txin.prevout) <= 1 || wtx.IsZerocoinSpend()) continue;  // No conflict if zero or one spends
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
bool CWallet::IsSpent(const uint256& hash, unsigned int n) const {
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

bool CWallet::EncryptWallet(const SecureString& strWalletPassphrase) {
  if (IsCrypted()) return false;

  CKeyingMaterial vMasterKey;
  RandAddSeedPerfmon();

  vMasterKey.resize(WALLET_CRYPTO_KEY_SIZE);
  GetRandBytes(&vMasterKey[0], WALLET_CRYPTO_KEY_SIZE);

  CMasterKey kMasterKey;
  RandAddSeedPerfmon();

  kMasterKey.vchSalt.resize(WALLET_CRYPTO_SALT_SIZE);
  GetRandBytes(&kMasterKey.vchSalt[0], WALLET_CRYPTO_SALT_SIZE);

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

  if (!crypter.SetKeyFromPassphrase(strWalletPassphrase, kMasterKey.vchSalt, kMasterKey.nDeriveIterations,
                                    kMasterKey.nDerivationMethod))
    return false;
  if (!crypter.Encrypt(vMasterKey, kMasterKey.vchCryptedKey)) return false;

  {
    LOCK(cs_wallet);
    mapMasterKeys[++nMasterKeyMaxID] = kMasterKey;
    if (fFileBacked) {
      assert(!pwalletdbEncryption);
      pwalletdbEncryption = new CWalletDB(strWalletFile);
      if (!pwalletdbEncryption->TxnBegin()) {
        delete pwalletdbEncryption;
        pwalletdbEncryption = nullptr;
        return false;
      }
      pwalletdbEncryption->WriteMasterKey(nMasterKeyMaxID, kMasterKey);
    }

    if (!EncryptKeys(vMasterKey)) {
      if (fFileBacked) {
        pwalletdbEncryption->TxnAbort();
        delete pwalletdbEncryption;
      }
      // We now probably have half of our keys encrypted in memory, and half not...
      // die and let the user reload their unencrypted wallet.
      assert(false);
    }

    // Encryption was introduced in version 0.4.0
    SetMinVersion(FEATURE_LATEST, pwalletdbEncryption, true);

    if (fFileBacked) {
      if (!pwalletdbEncryption->TxnCommit()) {
        delete pwalletdbEncryption;
        // We now have keys encrypted in memory, but not on disk...
        // die to avoid confusion and let the user reload their unencrypted wallet.
        assert(false);
      }

      delete pwalletdbEncryption;
      pwalletdbEncryption = nullptr;
    }

    Lock();
    Unlock(strWalletPassphrase);
    NewKeyPool();
    Lock();

    // Need to completely rewrite the wallet file; if we don't, bdb might keep
    // bits of the unencrypted private key in slack space in the database file.
    CDB::Rewrite(strWalletFile);
  }
  NotifyStatusChanged(this);

  return true;
}

int64_t CWallet::IncOrderPosNext(CWalletDB* pwalletdb) {
  AssertLockHeld(cs_wallet);  // nOrderPosNext
  int64_t nRet = nOrderPosNext++;
  if (pwalletdb) {
    pwalletdb->WriteOrderPosNext(nOrderPosNext);
  } else {
    CWalletDB(strWalletFile).WriteOrderPosNext(nOrderPosNext);
  }
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
    wtxOrdered.insert(make_pair(wtx.nOrderPos, TxPair(&wtx, (CAccountingEntry*)0)));
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
      wtxOrdered.insert(make_pair(wtx.nOrderPos, TxPair(&wtx, (CAccountingEntry*)0)));
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
    LogPrintf("AddToWallet %s  %s%s\n", wtxIn.GetHash().ToString(), (fInsertedNew ? "new" : ""),
              (fUpdated ? "update" : ""));

    // Write to disk
    if (fInsertedNew || fUpdated)
      if (!wtx.WriteToDisk()) return false;

    // Break debit/credit balance caches:
    wtx.MarkDirty();

    // Notify UI of new or updated transaction
    NotifyTransactionChanged(this, hash, fInsertedNew ? CT_NEW : CT_UPDATED);

    // notify an external script when a wallet transaction comes in or is updated
    std::string strCmd = GetArg("-walletnotify", "");

    if (!strCmd.empty()) {
      boost::replace_all(strCmd, "%s", wtxIn.GetHash().GetHex());
      boost::thread t(runCommand, strCmd);  // thread runs free
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
    if (!tx.IsZerocoinSpend() && mapWallet.count(txin.prevout.hash)) mapWallet[txin.prevout.hash].MarkDirty();
  }
}

void CWallet::EraseFromWallet(const uint256& hash) {
  if (!fFileBacked) return;
  {
    LOCK(cs_wallet);
    if (mapWallet.erase(hash)) CWalletDB(strWalletFile).EraseTx(hash);
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

bool CWallet::IsMyZerocoinSpend(const CBigNum& bnSerial) const { return zkpTracker->HasSerial(bnSerial); }

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
  bool fCheckZKP = GetBoolArg("-zapwallettxes", false);
  if (fCheckZKP) zkpTracker->Init();

  CBlockIndex* pindex = pindexStart;
  {
    LOCK2(cs_main, cs_wallet);

    // no need to read and scan block, if block was created before
    // our wallet birthday (as adjusted for block time variability)
    while (pindex && nTimeFirstKey && (pindex->GetBlockTime() < (nTimeFirstKey - 7200)) &&
           pindex->nHeight <= Params().Zerocoin_StartHeight())
      pindex = chainActive.Next(pindex);

    ShowProgress(_("Rescanning..."),
                 0);  // show rescan progress in GUI as dialog or on splashscreen, if -rescan on startup
    double dProgressStart = Checkpoints::GuessVerificationProgress(pindex, false);
    double dProgressTip = Checkpoints::GuessVerificationProgress(chainActive.Tip(), false);
    set<uint256> setAddedToWallet;
    while (pindex) {
      if (pindex->nHeight % 100 == 0 && dProgressTip - dProgressStart > 0.0)
        ShowProgress(
            _("Rescanning..."),
            std::max(1, std::min(99, (int)((Checkpoints::GuessVerificationProgress(pindex, false) - dProgressStart) /
                                           (dProgressTip - dProgressStart) * 100))));

      CBlock block;
      ReadBlockFromDisk(block, pindex);
      for (CTransaction& tx : block.vtx) {
        if (AddToWalletIfInvolvingMe(tx, &block, fUpdate)) ret++;
      }

      // If this is a zapwallettx, need to readd zkp
      if (fCheckZKP && pindex->nHeight >= Params().Zerocoin_StartHeight()) {
        list<CZerocoinMint> listMints;
        BlockToZerocoinMintList(block, listMints);

        for (auto& m : listMints) {
          if (IsMyMint(m.GetValue())) {
            LogPrint(ClubLog::ZERO, "%s: found mint\n", __func__);
            pwalletMain->UpdateMint(m.GetValue(), pindex->nHeight, m.GetTxHash(), m.GetDenomination());

            // Add the transaction to the wallet
            for (auto& tx : block.vtx) {
              uint256 txid = tx.GetHash();
              if (setAddedToWallet.count(txid) || mapWallet.count(txid)) continue;
              if (txid == m.GetTxHash()) {
                CWalletTx wtx(pwalletMain, tx);
                wtx.nTimeReceived = block.GetBlockTime();
                wtx.SetMerkleBranch(block);
                pwalletMain->AddToWallet(wtx);
                setAddedToWallet.insert(txid);
              }
            }

            // Check if the mint was ever spent
            int nHeightSpend = 0;
            uint256 txidSpend;
            CTransaction txSpend;
            if (IsSerialInBlockchain(GetSerialHash(m.GetSerialNumber()), nHeightSpend, txidSpend, txSpend)) {
              if (setAddedToWallet.count(txidSpend) || mapWallet.count(txidSpend)) continue;

              CWalletTx wtx(pwalletMain, txSpend);
              CBlockIndex* pindexSpend = chainActive[nHeightSpend];
              CBlock blockSpend;
              if (ReadBlockFromDisk(blockSpend, pindexSpend)) wtx.SetMerkleBranch(blockSpend);

              wtx.nTimeReceived = pindexSpend->nTime;
              pwalletMain->AddToWallet(wtx);
              setAddedToWallet.emplace(txidSpend);
            }
          }
        }
      }

      pindex = chainActive.Next(pindex);
      if (GetTime() >= nNow + 60) {
        nNow = GetTime();
        LogPrintf("Still rescanning. At block %d. Progress=%f\n", pindex->nHeight,
                  Checkpoints::GuessVerificationProgress(pindex));
      }
    }
    ShowProgress(_("Rescanning..."), 100);  // hide progress dialog in GUI
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
    multimap<unsigned int, CWalletTx*> mapSorted;
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
    for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
      const CWalletTx* pcoin = &(*it).second;

      if (pcoin->IsTrusted()) nTotal += pcoin->GetAvailableCredit();
    }
  }

  return nTotal;
}

std::map<libzerocoin::CoinDenomination, int> mapMintMaturity;
int nLastMaturityCheck = 0;
CAmount CWallet::GetZerocoinBalance(bool fMatureOnly) const {
  if (fMatureOnly) {
    if (chainActive.Height() > nLastMaturityCheck) mapMintMaturity = GetMintMaturityHeight();
    nLastMaturityCheck = chainActive.Height();

    CAmount nBalance = 0;
    vector<CMintMeta> vMints = zkpTracker->GetMints(true);
    for (auto meta : vMints) {
      if (meta.nHeight >= mapMintMaturity.at(meta.denom) || meta.nHeight >= chainActive.Height() || meta.nHeight == 0)
        continue;
      nBalance += libzerocoin::ZerocoinDenominationToAmount(meta.denom);
    }
    return nBalance;
  }

  return zkpTracker->GetBalance(false, false);
}

CAmount CWallet::GetImmatureZerocoinBalance() const {
  return GetZerocoinBalance(false) - GetZerocoinBalance(true) - GetUnconfirmedZerocoinBalance();
}

CAmount CWallet::GetUnconfirmedZerocoinBalance() const { return zkpTracker->GetUnconfirmedBalance(); }

CAmount CWallet::GetUnlockedCoins() const {
  //    if (fLiteMode) return 0;

  CAmount nTotal = 0;
  {
    LOCK2(cs_main, cs_wallet);
    for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
      const CWalletTx* pcoin = &(*it).second;

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
    for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
      const CWalletTx* pcoin = &(*it).second;

      if (pcoin->IsTrusted() && pcoin->GetDepthInMainChain() > 0) nTotal += pcoin->GetLockedCredit();
    }
  }

  return nTotal;
}

// Get a Map pairing the Denominations with the amount of Zerocoin for each Denomination
std::map<libzerocoin::CoinDenomination, CAmount> CWallet::GetMyZerocoinDistribution() const {
  std::map<libzerocoin::CoinDenomination, CAmount> spread;
  for (const auto& denom : libzerocoin::zerocoinDenomList)
    spread.insert(std::pair<libzerocoin::CoinDenomination, CAmount>(denom, 0));
  {
    LOCK(cs_wallet);
    set<CMintMeta> setMints = zkpTracker->ListMints(true, true, true);
    for (auto& mint : setMints) spread.at(mint.denom)++;
  }
  return spread;
}

CAmount CWallet::GetUnconfirmedBalance() const {
  CAmount nTotal = 0;
  {
    LOCK2(cs_main, cs_wallet);
    for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
      const CWalletTx* pcoin = &(*it).second;
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
    for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
      const CWalletTx* pcoin = &(*it).second;
      nTotal += pcoin->GetImmatureCredit();
    }
  }
  return nTotal;
}

CAmount CWallet::GetWatchOnlyBalance() const {
  CAmount nTotal = 0;
  {
    LOCK2(cs_main, cs_wallet);
    for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
      const CWalletTx* pcoin = &(*it).second;
      if (pcoin->IsTrusted()) nTotal += pcoin->GetAvailableWatchOnlyCredit();
    }
  }

  return nTotal;
}

CAmount CWallet::GetUnconfirmedWatchOnlyBalance() const {
  CAmount nTotal = 0;
  {
    LOCK2(cs_main, cs_wallet);
    for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
      const CWalletTx* pcoin = &(*it).second;
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
    for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
      const CWalletTx* pcoin = &(*it).second;
      nTotal += pcoin->GetImmatureWatchOnlyCredit();
    }
  }
  return nTotal;
}

CAmount CWallet::GetLockedWatchOnlyBalance() const {
  CAmount nTotal = 0;
  {
    LOCK2(cs_main, cs_wallet);
    for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
      const CWalletTx* pcoin = &(*it).second;
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
    for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
      const uint256& wtxid = it->first;
      const CWalletTx* pcoin = &(*it).second;

      if (!CheckFinalTx(*pcoin)) continue;

      if (fOnlyConfirmed && !pcoin->IsTrusted()) continue;

      if ((pcoin->IsCoinBase() || pcoin->IsCoinStake()) && pcoin->GetBlocksToMaturity() > 0) continue;

      int nDepth = pcoin->GetDepthInMainChain(false);
      // do not use IX for inputs that have less then 6 blockchain confirmations
      if (fUseIX && nDepth < 6) continue;

      // We should not consider coins which aren't at least in our mempool
      // It's possible for these to be conflicted via ancestors which we may never be able to detect
      if (nDepth == 0 && !pcoin->InMempool()) continue;

      for (unsigned int i = 0; i < pcoin->vout.size(); i++) {
        if (nCoinType == STAKABLE_COINS) {
          if (pcoin->vout[i].IsZerocoinMint()) continue;
        }

        isminetype mine = IsMine(pcoin->vout[i]);
        if (IsSpent(wtxid, i)) continue;
        if (mine == ISMINE_NO) continue;

        if ((mine == ISMINE_MULTISIG || mine == ISMINE_SPENDABLE) && nWatchonlyConfig == 2) continue;

        if (mine == ISMINE_WATCH_ONLY && nWatchonlyConfig == 1) continue;

        if (IsLockedCoin((*it).first, i)) continue;
        if (pcoin->vout[i].nValue <= 0 && !fIncludeZeroValue) continue;
        if (coinControl && coinControl->HasSelected() && !coinControl->fAllowOtherInputs &&
            !coinControl->IsSelected((*it).first, i))
          continue;

        bool fIsSpendable = false;
        if ((mine & ISMINE_SPENDABLE) != ISMINE_NO) fIsSpendable = true;
        if ((mine & ISMINE_MULTISIG) != ISMINE_NO) fIsSpendable = true;

        vCoins.emplace_back(COutput(pcoin, i, nDepth, fIsSpendable));
      }
    }
  }
}

map<CBitcoinAddress, vector<COutput> > CWallet::AvailableCoinsByAddress(bool fConfirmed, CAmount maxCoinValue) {
  vector<COutput> vCoins;
  AvailableCoins(vCoins, fConfirmed);

  map<CBitcoinAddress, vector<COutput> > mapCoins;
  for (COutput out : vCoins) {
    if (maxCoinValue > 0 && out.tx->vout[out.i].nValue > maxCoinValue) continue;

    CTxDestination address;
    if (!ExtractDestination(out.tx->vout[out.i].scriptPubKey, address)) continue;

    mapCoins[CBitcoinAddress(address)].push_back(out);
  }

  return mapCoins;
}

static void ApproximateBestSubset(vector<pair<CAmount, pair<const CWalletTx*, unsigned int> > > vValue,
                                  const CAmount& nTotalLower, const CAmount& nTargetValue, vector<char>& vfBest,
                                  CAmount& nBest, int iterations = 1000) {
  vector<char> vfIncluded;

  vfBest.assign(vValue.size(), true);
  nBest = nTotalLower;

  seed_insecure_rand();

  for (int nRep = 0; nRep < iterations && nBest != nTargetValue; nRep++) {
    vfIncluded.assign(vValue.size(), false);
    CAmount nTotal = 0;
    bool fReachedTarget = false;
    for (int nPass = 0; nPass < 2 && !fReachedTarget; nPass++) {
      for (unsigned int i = 0; i < vValue.size(); i++) {
        // The solver here uses a randomized algorithm,
        // the randomness serves no real security purpose but is just
        // needed to prevent degenerate behavior and it is important
        // that the rng is fast. We do not use a constant random sequence,
        // because there may be some privacy improvement by making
        // the selection random.
        if (nPass == 0 ? insecure_rand() & 1 : !vfIncluded[i]) {
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

bool CWallet::SelectStakeCoins(std::list<std::unique_ptr<CStakeInput> >& listInputs, CAmount nTargetAmount) {
  // Add Club
  vector<COutput> vCoins;
  AvailableCoins(vCoins, true, nullptr, false, STAKABLE_COINS);
  CAmount nAmountSelected = 0;
  if (GetBoolArg("-stake", true)) {
    for (const COutput& out : vCoins) {
      // make sure not to outrun target amount
      if (nAmountSelected + out.tx->vout[out.i].nValue > nTargetAmount) continue;

      // if zerocoinspend, then use the block time
      int64_t nTxTime = out.tx->GetTxTime();
      if (out.tx->IsZerocoinSpend()) {
        if (!out.tx->IsInMainChain()) continue;
        nTxTime = mapBlockIndex.at(out.tx->hashBlock)->GetBlockTime();
      }

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
  CAmount nBalance = GetBalance();
  CAmount nZkpBalance = GetZerocoinBalance(false);

  // Regular Club
  if (nBalance > 0) {
    if (gArgs.IsArgSet("-reservebalance") && !ParseMoney(gArgs.GetArg("-reservebalance", ""), nReserveBalance))
      return error("%s : invalid reserve balance amount", __func__);
    if (nBalance <= nReserveBalance) return false;

    vector<COutput> vCoins;
    AvailableCoins(vCoins, true);

    for (const COutput& out : vCoins) {
      int64_t nTxTime = out.tx->GetTxTime();
      if (out.tx->IsZerocoinSpend()) {
        if (!out.tx->IsInMainChain()) continue;
        nTxTime = mapBlockIndex.at(out.tx->hashBlock)->GetBlockTime();
      }

      if (GetAdjustedTime() - nTxTime > Params().StakeMinAge()) return true;
    }
  }

  // ZKP
  if (nZkpBalance > 0) return true;
  return false;
}

bool CWallet::SelectCoinsMinConf(const CAmount& nTargetValue, int nConfMine, int nConfTheirs, vector<COutput> vCoins,
                                 set<pair<const CWalletTx*, unsigned int> >& setCoinsRet, CAmount& nValueRet) const {
  setCoinsRet.clear();
  nValueRet = 0;

  // List of values less than target
  pair<CAmount, pair<const CWalletTx*, unsigned int> > coinLowestLarger;
  coinLowestLarger.first = std::numeric_limits<CAmount>::max();
  coinLowestLarger.second.first = nullptr;
  vector<pair<CAmount, pair<const CWalletTx*, unsigned int> > > vValue;
  CAmount nTotalLower = 0;

  random_shuffle(vCoins.begin(), vCoins.end(), GetRandInt);

#warning "Refactoring done here - need to test"

  vValue.clear();
  nTotalLower = 0;
  for (const COutput& output : vCoins) {
    if (!output.fSpendable) continue;
    const CWalletTx* pcoin = output.tx;
    if (output.nDepth < (pcoin->IsFromMe(ISMINE_ALL) ? nConfMine : nConfTheirs)) continue;
    int i = output.i;
    CAmount n = pcoin->vout[i].nValue;

    pair<CAmount, pair<const CWalletTx*, unsigned int> > coin = make_pair(n, make_pair(pcoin, i));

    if (n == nTargetValue) {
      setCoinsRet.insert(coin.second);
      nValueRet += coin.first;
      return true;
    } else if (n < nTargetValue + CENT) {
      vValue.push_back(coin);
      nTotalLower += n;
    } else if (n < coinLowestLarger.first) {
      coinLowestLarger = coin;
    }
  }

  if (nTotalLower == nTargetValue) {
    for (unsigned int i = 0; i < vValue.size(); ++i) {
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
  if (nBest != nTargetValue && nTotalLower >= nTargetValue + CENT)
    ApproximateBestSubset(vValue, nTotalLower, nTargetValue + CENT, vfBest, nBest, 1000);

  // If we have a bigger coin and (either the stochastic approximation didn't find a good solution,
  //                                   or the next bigger coin is closer), return the bigger coin
  if (coinLowestLarger.second.first &&
      ((nBest != nTargetValue && nBest < nTargetValue + CENT) || coinLowestLarger.first <= nBest)) {
    setCoinsRet.insert(coinLowestLarger.second);
    nValueRet += coinLowestLarger.first;
  } else {
    string s = "CWallet::SelectCoinsMinConf best subset: ";
    for (unsigned int i = 0; i < vValue.size(); i++) {
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

bool CWallet::SelectCoins(const CAmount& nTargetValue, set<pair<const CWalletTx*, unsigned int> >& setCoinsRet,
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
    for (map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); ++it) {
      const CWalletTx* pcoin = &(*it).second;
      if (pcoin->IsTrusted()) {
        int nDepth = pcoin->GetDepthInMainChain(false);

        for (unsigned int i = 0; i < pcoin->vout.size(); i++) {
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
  if (useIX && nFeePay < CENT) nFeePay = CENT;

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
        set<pair<const CWalletTx*, unsigned int> > setCoins;
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
          // change transaction isn't always pay-to-club-address
          CScript scriptChange;
          bool combineChange = false;

          // coin control: send change to custom address
          if (coinControl && !boost::get<CNoDestination>(&coinControl->destChange)) {
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
        unsigned int nBytes = ::GetSerializeSize(*(CTransaction*)&wtxNew);
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

        CAmount nFeeNeeded = max(nFeePay, GetMinimumFee(nBytes, nTxConfirmTarget, mempool));

        // If we made it here and we aren't even able to meet the relay fee on the next pass, give up
        // because we must be at the maximum allowed fee.
        if (nFeeNeeded < ::minRelayTxFee.GetFee(nBytes)) {
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
bool CWallet::CreateCoinStake(const CKeyStore& keystore, unsigned int nBits, int64_t nSearchInterval,
                              CMutableTransaction& txNew, unsigned int& nTxNewTime) {
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

  if (gArgs.IsArgSet("-reservebalance") && !ParseMoney(gArgs.GetArg("-reservebalance", ""), nReserveBalance))
    return error("CreateCoinStake : invalid reserve balance amount");

  if (nBalance > 0 && nBalance <= nReserveBalance) return false;

  // Get the list of stakable inputs
  std::list<std::unique_ptr<CStakeInput> > listInputs;
  if (!SelectStakeCoins(listInputs, nBalance - nReserveBalance)) return false;

  if (listInputs.empty()) return false;

  if (GetAdjustedTime() - chainActive.Tip()->GetBlockTime() < 60) MilliSleep(10000);

  CAmount nCredit = 0;
  CScript scriptPubKeyKernel;
  bool fKernelFound = false;
  for (std::unique_ptr<CStakeInput>& stakeInput : listInputs) {
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
    uint256 hashProofOfStake(uint256S("0"));
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

      // Calculate reward
      CAmount nReward;
      nReward = GetBlockValue(chainActive.Height() + 1);
      nCredit += nReward;

      // Create the output transaction(s)
      vector<CTxOut> vout;
      if (!stakeInput->CreateTxOuts(this, vout, nCredit)) {
        LogPrintf("%s : failed to get scriptPubKey\n", __func__);
        continue;
      }
      txNew.vout.insert(txNew.vout.end(), vout.begin(), vout.end());

      CAmount nMinFee = 0;
      if (!stakeInput->IsZKP()) {
        // Set output amount
        if (txNew.vout.size() == 3) {
          txNew.vout[1].nValue = ((nCredit - nMinFee) / 2 / CENT) * CENT;
          txNew.vout[2].nValue = nCredit - nMinFee - txNew.vout[1].nValue;
        } else
          txNew.vout[1].nValue = nCredit - nMinFee;
      }

      // Limit size
      unsigned int nBytes = ::GetSerializeSize(txNew);
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

      // Mark mints as spent
      if (stakeInput->IsZKP()) { return true; }

      fKernelFound = true;
      break;
    }
    if (fKernelFound) break;  // if kernel is found stop searching
  }
  if (!fKernelFound) return false;

  // Sign for Club
  int nIn = 0;
  if (!txNew.vin[0].scriptSig.IsZerocoinSpend()) {
    for (CTxIn txIn : txNew.vin) {
      const CWalletTx* wtx = GetWalletTx(txIn.prevout.hash);
      if (!SignSignature(*this, *wtx, txNew, nIn++)) return error("CreateCoinStake : failed to sign coinstake");
    }
  } else {
    // Update the mint database with tx hash and height
    for (const CTxOut& out : txNew.vout) {
      if (!out.IsZerocoinMint()) continue;

      libzerocoin::PublicCoin pubcoin;
      CValidationState state;
      if (!TxOutToPublicCoin(out, pubcoin, state)) return error("%s: extracting pubcoin from txout failed", __func__);

      uint256 hashPubcoin = GetPubCoinHash(pubcoin.getValue());
      if (!zkpTracker->HasPubcoinHash(hashPubcoin))
        return error("%s: could not find pubcoinhash %s in tracker", __func__, hashPubcoin.GetHex());

      CMintMeta meta = zkpTracker->GetMetaFromPubcoin(hashPubcoin);
      meta.txid = txNew.GetHash();
      meta.nHeight = chainActive.Height() + 1;
      if (!zkpTracker->UpdateState(meta)) return error("%s: failed to update metadata in tracker", __func__);
    }
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
      // This is only to keep the database open to defeat the auto-flush for the
      // duration of this scope.  This is the only place where this optimization
      // maybe makes sense; please don't do it anywhere else.
      CWalletDB* pwalletdb = fFileBacked ? new CWalletDB(strWalletFile, "r") : nullptr;

      // Take key pair from key pool so it won't be used again
      reservekey.KeepKey();

      // Add tx to wallet, because if it has change it's also ours,
      // otherwise just for transaction history.
      AddToWallet(wtxNew);

      // Notify that old coins are spent
      if (!wtxNew.IsZerocoinSpend()) {
        set<uint256> updated_hahes;
        for (const CTxIn& txin : wtxNew.vin) {
          // notify only once
          if (updated_hahes.find(txin.prevout.hash) != updated_hahes.end()) continue;

          CWalletTx& coin = mapWallet[txin.prevout.hash];
          coin.BindWallet(this);
          NotifyTransactionChanged(this, txin.prevout.hash, CT_UPDATED);
          updated_hahes.insert(txin.prevout.hash);
        }
      }

      if (fFileBacked) delete pwalletdb;
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

bool CWallet::AddAccountingEntry(const CAccountingEntry& acentry, CWalletDB& pwalletdb) {
  if (!pwalletdb.WriteAccountingEntry_Backend(acentry)) return false;

  laccentries.push_back(acentry);
  CAccountingEntry& entry = laccentries.back();
  wtxOrdered.insert(make_pair(entry.nOrderPos, TxPair((CWalletTx*)0, &entry)));

  return true;
}

CAmount CWallet::GetMinimumFee(unsigned int nTxBytes, unsigned int nConfirmTarget, const CTxMemPool& pool) {
  // payTxFee is user-set "I want to pay this much"
  CAmount nFeeNeeded = payTxFee.GetFee(nTxBytes);
  // user selected total at least (default=true)
  if (fPayAtLeastCustomFee && nFeeNeeded > 0 && nFeeNeeded < payTxFee.GetFeePerK()) nFeeNeeded = payTxFee.GetFeePerK();
  // User didn't set: use -txconfirmtarget to estimate...
  if (nFeeNeeded == 0) nFeeNeeded = pool.estimateFee(nConfirmTarget).GetFee(nTxBytes);
  // ... unless we don't have enough mempool data, in which case fall
  // back to a hard-coded fee
  if (nFeeNeeded == 0) nFeeNeeded = minTxFee.GetFee(nTxBytes);
  // prevent user from paying a non-sense fee (like 1 satoshi): 0 < fee < minRelayFee
  if (nFeeNeeded < ::minRelayTxFee.GetFee(nTxBytes)) nFeeNeeded = ::minRelayTxFee.GetFee(nTxBytes);
  // But always obey the maximum
  if (nFeeNeeded > maxTxFee) nFeeNeeded = maxTxFee;
  return nFeeNeeded;
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
  DBErrors nLoadWalletRet = CWalletDB(strWalletFile, "cr+").LoadWallet(this);
  if (nLoadWalletRet == DB_NEED_REWRITE) {
    if (CDB::Rewrite(strWalletFile, "\x04pool")) {
      LOCK(cs_wallet);
      setKeyPool.clear();
      // Note: can't top-up keypool here, because wallet is locked.
      // User will be prompted to unlock wallet the next operation
      // the requires a new key.
    }
  }

  if (nLoadWalletRet != DB_LOAD_OK) return nLoadWalletRet;
  fFirstRunRet = !vchDefaultKey.IsValid();

  uiInterface.LoadWallet(this);

  return DB_LOAD_OK;
}

DBErrors CWallet::ZapWalletTx(std::vector<CWalletTx>& vWtx) {
  if (!fFileBacked) return DB_LOAD_OK;
  DBErrors nZapWalletTxRet = CWalletDB(strWalletFile, "cr+").ZapWalletTx(this, vWtx);
  if (nZapWalletTxRet == DB_NEED_REWRITE) {
    if (CDB::Rewrite(strWalletFile, "\x04pool")) {
      LOCK(cs_wallet);
      setKeyPool.clear();
      // Note: can't top-up keypool here, because wallet is locked.
      // User will be prompted to unlock wallet the next operation
      // that requires a new key.
    }
  }

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
  NotifyAddressBookChanged(this, address, strName, ::IsMine(*this, address) != ISMINE_NO, strPurpose,
                           (fUpdated ? CT_UPDATED : CT_NEW));
  if (!fFileBacked) return false;
  if (!strPurpose.empty() && !CWalletDB(strWalletFile).WritePurpose(CBitcoinAddress(address).ToString(), strPurpose))
    return false;
  return CWalletDB(strWalletFile).WriteName(CBitcoinAddress(address).ToString(), strName);
}

bool CWallet::DelAddressBook(const CTxDestination& address) {
  {
    LOCK(cs_wallet);  // mapAddressBook

    if (fFileBacked) {
      // Delete destdata tuples associated with address
      std::string strAddress = CBitcoinAddress(address).ToString();
      for (const auto& item : mapAddressBook[address].destdata) {
        CWalletDB(strWalletFile).EraseDestData(strAddress, item.first);
      }
    }
    mapAddressBook.erase(address);
  }

  NotifyAddressBookChanged(this, address, "", ::IsMine(*this, address) != ISMINE_NO, "", CT_DELETED);

  if (!fFileBacked) return false;
  CWalletDB(strWalletFile).ErasePurpose(CBitcoinAddress(address).ToString());
  return CWalletDB(strWalletFile).EraseName(CBitcoinAddress(address).ToString());
}

bool CWallet::SetDefaultKey(const CPubKey& vchPubKey) {
  if (fFileBacked) {
    if (!CWalletDB(strWalletFile).WriteDefaultKey(vchPubKey)) return false;
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
    CWalletDB walletdb(strWalletFile);
    for (int64_t nIndex : setKeyPool) walletdb.ErasePool(nIndex);
    setKeyPool.clear();

    if (IsLocked()) return false;

    int64_t nKeys = max(GetArg("-keypool", KEY_RES_SIZE), (int64_t)0);
    for (int i = 0; i < nKeys; i++) {
      int64_t nIndex = i + 1;
      walletdb.WritePool(nIndex, CKeyPool(GenerateNewKey()));
      setKeyPool.insert(nIndex);
    }
    LogPrintf("CWallet::NewKeyPool wrote %d new keys\n", nKeys);
  }
  return true;
}

bool CWallet::TopUpKeyPool(unsigned int kpSize) {
  {
    LOCK(cs_wallet);

    if (IsLocked()) return false;

    CWalletDB walletdb(strWalletFile);

    // Top up key pool
    unsigned int nTargetSize;
    if (kpSize > 0)
      nTargetSize = kpSize;
    else
      nTargetSize = max(GetArg("-keypool", KEY_RES_SIZE), (int64_t)0);

    while (setKeyPool.size() < (nTargetSize + 1)) {
      int64_t nEnd = 1;
      if (!setKeyPool.empty()) nEnd = *(--setKeyPool.end()) + 1;
      if (!walletdb.WritePool(nEnd, CKeyPool(GenerateNewKey())))
        throw runtime_error("TopUpKeyPool() : writing generated key failed");
      setKeyPool.insert(nEnd);
      LogPrintf("keypool added key %d, size=%u\n", nEnd, setKeyPool.size());
      double dProgress = 100.f * nEnd / (nTargetSize + 1);
      std::string strMsg = strprintf(_("Loading wallet... (%3.2f %%)"), dProgress);
      uiInterface.InitMessage(strMsg);
    }
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

    CWalletDB walletdb(strWalletFile);

    nIndex = *(setKeyPool.begin());
    setKeyPool.erase(setKeyPool.begin());
    if (!walletdb.ReadPool(nIndex, keypool)) throw runtime_error("ReserveKeyFromKeyPool() : read failed");
    if (!HaveKey(keypool.vchPubKey.GetID())) throw runtime_error("ReserveKeyFromKeyPool() : unknown key in key pool");
    assert(keypool.vchPubKey.IsValid());
    LogPrintf("keypool reserve %d\n", nIndex);
  }
}

void CWallet::KeepKey(int64_t nIndex) {
  // Remove from key pool
  if (fFileBacked) {
    CWalletDB walletdb(strWalletFile);
    walletdb.ErasePool(nIndex);
  }
  LogPrintf("keypool keep %d\n", nIndex);
}

void CWallet::ReturnKey(int64_t nIndex) {
  // Return to key pool
  {
    LOCK(cs_wallet);
    setKeyPool.insert(nIndex);
  }
  LogPrintf("keypool return %d\n", nIndex);
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

      for (unsigned int i = 0; i < pcoin->vout.size(); i++) {
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
    for (unsigned int i = 0; i < pcoin->vout.size(); i++)
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
    for (CTxDestination address : grouping)
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
    for (CTxDestination element : *merged) setmap[element] = merged;
  }

  set<set<CTxDestination> > ret;
  for (set<CTxDestination>* uniqueGrouping : uniqueGroupings) {
    ret.insert(*uniqueGrouping);
    delete uniqueGrouping;
  }

  return ret;
}

set<CTxDestination> CWallet::GetAccountAddresses(string strAccount) const {
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

  CWalletDB walletdb(strWalletFile);

  LOCK2(cs_main, cs_wallet);
  for (const int64_t& id : setKeyPool) {
    CKeyPool keypool;
    if (!walletdb.ReadPool(id, keypool)) throw runtime_error("GetAllReserveKeyHashes() : read failed");
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
      NotifyTransactionChanged(this, hashTx, CT_UPDATED);
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

bool CWallet::IsLockedCoin(uint256 hash, unsigned int n) const {
  AssertLockHeld(cs_wallet);  // setLockedCoins
  COutPoint outpt(hash, n);

  return (setLockedCoins.count(outpt) > 0);
}

void CWallet::ListLockedCoins(std::vector<COutPoint>& vOutpts) {
  AssertLockHeld(cs_wallet);  // setLockedCoins
  for (std::set<COutPoint>::iterator it = setLockedCoins.begin(); it != setLockedCoins.end(); it++) {
    COutPoint outpt = (*it);
    vOutpts.push_back(outpt);
  }
}

/** @} */  // end of Actions

class CAffectedKeysVisitor : public boost::static_visitor<void> {
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
      for (const CTxDestination& dest : vDest) boost::apply_visitor(*this, dest);
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
  for (std::map<CKeyID, CKeyMetadata>::const_iterator it = mapKeyMetadata.begin(); it != mapKeyMetadata.end(); it++)
    if (it->second.nCreateTime) mapKeyBirth[it->first] = it->second.nCreateTime;

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
  for (std::map<uint256, CWalletTx>::const_iterator it = mapWallet.begin(); it != mapWallet.end(); it++) {
    // iterate over all wallet transactions...
    const CWalletTx& wtx = (*it).second;
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
  for (std::map<CKeyID, CBlockIndex*>::const_iterator it = mapKeyFirstBlock.begin(); it != mapKeyFirstBlock.end(); it++)
    mapKeyBirth[it->first] = it->second->GetBlockTime() - 7200;  // block times can be 2h off
}

unsigned int CWallet::ComputeTimeSmart(const CWalletTx& wtx) const {
  unsigned int nTimeSmart = wtx.nTimeReceived;
  if (!wtx.hashBlock.IsNull()) {
    if (mapBlockIndex.count(wtx.hashBlock)) {
      int64_t latestNow = wtx.nTimeReceived;
      int64_t latestEntry = 0;
      {
        // Tolerate times up to the last timestamp in the wallet not more than 5 minutes into the future
        int64_t latestTolerated = latestNow + 300;
        TxItems txOrdered = wtxOrdered;
        for (TxItems::reverse_iterator it = txOrdered.rbegin(); it != txOrdered.rend(); ++it) {
          CWalletTx* const pwtx = (*it).second.first;
          if (pwtx == &wtx) continue;
          CAccountingEntry* const pacentry = (*it).second.second;
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
  if (boost::get<CNoDestination>(&dest)) return false;

  mapAddressBook[dest].destdata.insert(std::make_pair(key, value));
  if (!fFileBacked) return true;
  return CWalletDB(strWalletFile).WriteDestData(CBitcoinAddress(dest).ToString(), key, value);
}

bool CWallet::EraseDestData(const CTxDestination& dest, const std::string& key) {
  if (!mapAddressBook[dest].destdata.erase(key)) return false;
  if (!fFileBacked) return true;
  return CWalletDB(strWalletFile).EraseDestData(CBitcoinAddress(dest).ToString(), key);
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
  if (chainActive.Tip()->nTime < (GetAdjustedTime() - 300) || IsLocked()) { return; }

  map<CBitcoinAddress, vector<COutput> > mapCoinsByAddress =
      AvailableCoinsByAddress(true, nAutoCombineThreshold * COIN);

  // coins are sectioned by address. This combination code only wants to combine inputs that belong to the same address
  for (map<CBitcoinAddress, vector<COutput> >::iterator it = mapCoinsByAddress.begin(); it != mapCoinsByAddress.end();
       it++) {
    vector<COutput> vCoins, vRewardCoins;
    vCoins = it->second;

    // We don't want the tx to be refused for being too large
    // we use 50 bytes as a base tx size (2 output: 2*34 + overhead: 10 -> 90 to be certain)
    unsigned int txSizeEstimate = 90;

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
    CScript scriptPubKey = GetScriptForDestination(it->first.Get());
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
      for (unsigned int i = 0; i < vDisabledAddresses.size(); i++) {
        if (vDisabledAddresses[i] == CBitcoinAddress(destMyAddress).ToString()) {
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
    for (unsigned int i = 0; i < vMultiSend.size(); i++) {
      // MultiSend vector is a pair of 1)Address as a std::string 2) Percent of stake to send as an int
      nAmount = ((out.tx->GetCredit(filter) - out.tx->GetDebit(filter)) * vMultiSend[i].second) / 100;
      CBitcoinAddress strAddSend(vMultiSend[i].first);
      CScript scriptPubKey;
      scriptPubKey = GetScriptForDestination(strAddSend.Get());
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
    CWalletDB walletdb(strWalletFile);
    nLastMultiSendHeight = chainActive.Tip()->nHeight;
    if (!walletdb.WriteMSettings(fMultiSendStake, false, nLastMultiSendHeight))
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

CWalletKey::CWalletKey(int64_t nExpires) {
  nTimeCreated = (nExpires ? GetTime() : 0);
  nTimeExpires = nExpires;
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
  if (fLargeWorkForkFound || fLargeWorkInvalidChainFound) return -2;
  return -1;
}

bool CMerkleTx::IsTransactionLockTimedOut() const { return 0; }

// Given a set of inputs, find the public key that contributes the most coins to the input set
CScript GetLargestContributor(set<pair<const CWalletTx*, unsigned int> >& setCoins) {
  map<CScript, CAmount> mapScriptsOut;
  for (const std::pair<const CWalletTx*, unsigned int>& coin : setCoins) {
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

bool CWallet::GetZerocoinKey(const CBigNum& bnSerial, CKey& key) {
  CWalletDB walletdb(strWalletFile);
  CZerocoinMint mint;
  if (!GetMint(GetSerialHash(bnSerial), mint))
    return error("%s: could not find serial %s in walletdb!", __func__, bnSerial.GetHex());

  return mint.GetKeyPair(key);
}

bool CWallet::CreateZKPOutPut(libzerocoin::CoinDenomination denomination, CTxOut& outMint, CDeterministicMint& dMint) {
  // mint a new coin (create Pedersen Commitment) and extract PublicCoin that is shareable from it
  libzerocoin::PrivateCoin coin(libzerocoin::gpZerocoinParams);
  zwalletMain->GenerateDeterministicZKP(denomination, coin, dMint);

  libzerocoin::PublicCoin pubCoin = coin.getPublicCoin();

  // Validate
  if (!pubCoin.validate()) { return error("%s: newly created pubcoin is not valid", __func__); }

  zwalletMain->UpdateCount();

  CScript scriptSerializedCoin = CScript() << OP_ZEROCOINMINT << pubCoin.getValue().getvch().size()
                                           << pubCoin.getValue().getvch();
  outMint = CTxOut(libzerocoin::ZerocoinDenominationToAmount(denomination), scriptSerializedCoin);

  return true;
}

bool CWallet::CreateZerocoinMintTransaction(const CAmount nValue, CMutableTransaction& txNew,
                                            vector<CDeterministicMint>& vDMints, CReserveKey* reservekey,
                                            int64_t& nFeeRet, std::string& strFailReason,
                                            const CCoinControl* coinControl, const bool isZCSpendChange) {
  if (IsLocked()) {
    strFailReason = _("Error: Wallet locked, unable to create transaction!");
    LogPrintf("SpendZerocoin() : %s", strFailReason.c_str());
    return false;
  }

  // add multiple mints that will fit the amount requested as closely as possible
  CAmount nMintingValue = 0;
  CAmount nValueRemaining = 0;
  while (true) {
    // mint a coin with the closest denomination to what is being requested
    nFeeRet = max(static_cast<int>(txNew.vout.size()), 1) * Params().Zerocoin_MintFee();
    nValueRemaining = nValue - nMintingValue - (isZCSpendChange ? nFeeRet : 0);

    // if this is change of a zerocoinspend, then we can't mint all change, at least something must be given as a fee
    if (isZCSpendChange && nValueRemaining <= 1 * COIN) break;

    libzerocoin::CoinDenomination denomination =
        libzerocoin::AmountToClosestDenomination(nValueRemaining, nValueRemaining);
    if (denomination == libzerocoin::ZQ_ERROR) break;

    CAmount nValueNewMint = libzerocoin::ZerocoinDenominationToAmount(denomination);
    nMintingValue += nValueNewMint;

    CTxOut outMint;
    CDeterministicMint dMint;
    if (!CreateZKPOutPut(denomination, outMint, dMint)) {
      strFailReason = strprintf("%s: failed to create new zkp output", __func__);
      return error(strFailReason.c_str());
    }
    txNew.vout.push_back(outMint);

    // store as CZerocoinMint for later use
    LogPrint(ClubLog::ZERO, "%s: new mint %s\n", __func__, dMint.ToString());
    vDMints.emplace_back(dMint);
  }

  // calculate fee
  CAmount nFee = Params().Zerocoin_MintFee() * txNew.vout.size();

  // no ability to select more coins if this is a ZCSpend change mint
  CAmount nTotalValue = (isZCSpendChange ? nValue : (nValue + nFee));

  // check for a zerocoinspend that mints the change
  CAmount nValueIn = 0;
  set<pair<const CWalletTx*, unsigned int> > setCoins;
  if (isZCSpendChange) {
    nValueIn = nValue;
  } else {
    // select UTXO's to use
    if (!SelectCoins(nTotalValue, setCoins, nValueIn, coinControl)) {
      strFailReason =
          _("Insufficient or insufficient confirmed funds, you might need to wait a few minutes and try again.");
      return false;
    }

    // Fill vin
    for (const std::pair<const CWalletTx*, unsigned int>& coin : setCoins)
      txNew.vin.push_back(CTxIn(coin.first->GetHash(), coin.second));
  }

  // any change that is less than 0.0100000 will be ignored and given as an extra fee
  // also assume that a zerocoinspend that is minting the change will not have any change that goes to Club
  CAmount nChange = nValueIn - nTotalValue;  // Fee already accounted for in nTotalValue
  if (nChange > 1 * CENT && !isZCSpendChange) {
    // Fill a vout to ourself using the largest contributing address
    CScript scriptChange = GetLargestContributor(setCoins);

    // add to the transaction
    CTxOut outChange(nChange, scriptChange);
    txNew.vout.push_back(outChange);
  } else {
    if (reservekey) reservekey->ReturnKey();
  }

  // Sign if these are club outputs - NOTE that ZKP outputs are signed later in SoK
  if (!isZCSpendChange) {
    int nIn = 0;
    for (const std::pair<const CWalletTx*, unsigned int>& coin : setCoins) {
      if (!SignSignature(*this, *coin.first, txNew, nIn++)) {
        strFailReason = _("Signing transaction failed");
        return false;
      }
    }
  }

  return true;
}

bool CWallet::MintToTxIn(CZerocoinMint zerocoinSelected, int nSecurityLevel, const uint256& hashTxOut, CTxIn& newTxIn,
                         CZerocoinSpendReceipt& receipt, libzerocoin::SpendType spendType,
                         CBlockIndex* pindexCheckpoint) {
  // Default error status if not changed below
  receipt.SetStatus(_("Transaction Mint Started"), ZKP_TXMINT_GENERAL);
  libzerocoin::ZerocoinParams* paramsAccumulator = libzerocoin::gpZerocoinParams;
  libzerocoin::ZerocoinParams* paramsCoin = libzerocoin::gpZerocoinParams;

  // 2. Get pubcoin from the private coin
  libzerocoin::CoinDenomination denomination = zerocoinSelected.GetDenomination();
  libzerocoin::PublicCoin pubCoinSelected(zerocoinSelected.GetValue(), denomination);
  // LogPrintf("%s : selected mint %s\n pubcoinhash=%s\n", __func__, zerocoinSelected.ToString(),
  // GetPubCoinHash(zerocoinSelected.GetValue()).GetHex());
  if (!pubCoinSelected.validate()) {
    receipt.SetStatus(_("The selected mint coin is an invalid coin"), ZKP_INVALID_COIN);
    return false;
  }

  // 3. Compute Accumulator and Witness
  libzerocoin::Accumulator accumulator(paramsAccumulator, pubCoinSelected.getDenomination());
  libzerocoin::AccumulatorWitness witness(paramsAccumulator, accumulator, pubCoinSelected);
  string strFailReason = "";
  int nMintsAdded = 0;
  if (!GenerateAccumulatorWitness(pubCoinSelected, accumulator, witness, nSecurityLevel, nMintsAdded, strFailReason,
                                  pindexCheckpoint)) {
    receipt.SetStatus(_("Try to spend with a higher security level to include more coins"),
                      ZKP_FAILED_ACCUMULATOR_INITIALIZATION);
    return error("%s : %s", __func__, receipt.GetStatusMessage());
  }

  // Construct the CoinSpend object. This acts like a signature on the transaction.
  libzerocoin::PrivateCoin privateCoin(paramsCoin);
  privateCoin.setPublicCoin(pubCoinSelected);
  privateCoin.setRandomness(zerocoinSelected.GetRandomness());
  privateCoin.setSerialNumber(zerocoinSelected.GetSerialNumber());

  // zerocoins have a privkey associated with them
  uint8_t nVersion = zerocoinSelected.GetVersion();
  privateCoin.setVersion(zerocoinSelected.GetVersion());
  LogPrintf("%s: privatecoin version=%d\n", __func__, privateCoin.getVersion());
  CKey key;
  if (!zerocoinSelected.GetKeyPair(key))
    return error("%s: failed to set ZKP privkey mint version=%d", __func__, nVersion);

  privateCoin.setPrivKey(key.GetPrivKey());

  uint32_t nChecksum = GetChecksum(accumulator.getValue());
  CBigNum bnValue;
  if (!GetAccumulatorValueFromChecksum(nChecksum, false, bnValue) || bnValue == 0)
    return error("%s: could not find checksum used for spend\n", __func__);

  try {
    libzerocoin::CoinSpend spend(paramsCoin, privateCoin, accumulator, nChecksum, witness, hashTxOut);
    //                              spendType);

    // LogPrintf("%s\n", spend.ToString());

    if (!spend.Verify(accumulator)) {
      receipt.SetStatus(_("The new spend coin transaction did not verify"), ZKP_INVALID_WITNESS);
      // return false;
      LogPrintf("** spend.verify failed, trying with different params\n");

      libzerocoin::CoinSpend spend2(libzerocoin::gpZerocoinParams, privateCoin, accumulator, nChecksum, witness,
                                    hashTxOut);  //, libzerocoin::SpendType::SPEND);
      LogPrintf("*** spend2 valid=%d\n", spend2.Verify(accumulator));
      return false;
    }

    // Deserialize the CoinSpend intro a fresh object
    CDataStream serializedCoinSpend(SER_NETWORK, PROTOCOL_VERSION);
    serializedCoinSpend << spend;
    std::vector<unsigned char> data(serializedCoinSpend.begin(), serializedCoinSpend.end());

    // Add the coin spend into a Club transaction
    newTxIn.scriptSig = CScript() << OP_ZEROCOINSPEND << data.size();
    newTxIn.scriptSig.insert(newTxIn.scriptSig.end(), data.begin(), data.end());
    newTxIn.prevout.SetNull();

    // use nSequence as a shorthand lookup of denomination
    // NOTE that this should never be used in place of checking the value in the final blockchain
    // acceptance/verification of the transaction
    newTxIn.nSequence = denomination;

    CDataStream serializedCoinSpendChecking(SER_NETWORK, PROTOCOL_VERSION);
    try {
      serializedCoinSpendChecking << spend;
    } catch (...) {
      receipt.SetStatus(_("Failed to deserialize"), ZKP_BAD_SERIALIZATION);
      return false;
    }

    libzerocoin::CoinSpend newSpendChecking(paramsCoin, serializedCoinSpendChecking);
    if (!newSpendChecking.Verify(accumulator)) {
      receipt.SetStatus(_("The transaction did not verify"), ZKP_BAD_SERIALIZATION);
      return false;
    }

    if (IsSerialKnown(spend.getCoinSerialNumber())) {
      // Tried to spend an already spent ZKP
      receipt.SetStatus(_("The coin spend has been used"), ZKP_SPENT_USED_ZKP);

      uint256 hashSerial = GetSerialHash(spend.getCoinSerialNumber());
      if (!zkpTracker->HasSerialHash(hashSerial))
        return error("%s: serialhash %s not found in tracker", __func__, hashSerial.GetHex());

      CMintMeta meta = zkpTracker->Get(hashSerial);
      meta.isUsed = true;
      if (!zkpTracker->UpdateState(meta)) LogPrintf("%s: failed to write zerocoinmint\n", __func__);

      pwalletMain->NotifyZerocoinChanged(pwalletMain, zerocoinSelected.GetValue().GetHex(), "Used", CT_UPDATED);
      return false;
    }

    uint32_t nAccumulatorChecksum = GetChecksum(accumulator.getValue());
    CZerocoinSpend zcSpend(spend.getCoinSerialNumber(), uint256S("0"), zerocoinSelected.GetValue(),
                           zerocoinSelected.GetDenomination(), nAccumulatorChecksum);
    zcSpend.SetMintCount(nMintsAdded);
    receipt.AddSpend(zcSpend);
  } catch (const std::exception&) {
    receipt.SetStatus(_("CoinSpend: Accumulator witness does not verify"), ZKP_INVALID_WITNESS);
    return false;
  }

  receipt.SetStatus(_("Spend Valid"), ZKP_SPEND_OKAY);  // Everything okay

  return true;
}

bool CWallet::CreateZerocoinSpendTransaction(CAmount nValue, int nSecurityLevel, CWalletTx& wtxNew,
                                             CReserveKey& reserveKey, CZerocoinSpendReceipt& receipt,
                                             vector<CZerocoinMint>& vSelectedMints,
                                             vector<CDeterministicMint>& vNewMints, bool fMintChange,
                                             bool fMinimizeChange, CBitcoinAddress* address) {
  // Check available funds
  int nStatus = ZKP_TRX_FUNDS_PROBLEMS;
  if (nValue > GetZerocoinBalance(true)) {
    receipt.SetStatus(_("You don't have enough Zerocoins in your wallet"), nStatus);
    return false;
  }

  if (nValue < 1) {
    receipt.SetStatus(_("Value is below the smallest available denomination (= 1) of ZKP"), nStatus);
    return false;
  }

  // Create transaction
  nStatus = ZKP_TRX_CREATE;

  // If not already given pre-selected mints, then select mints from the wallet
  CWalletDB walletdb(pwalletMain->strWalletFile);
  set<CMintMeta> setMints;
  CAmount nValueSelected = 0;
  int nCoinsReturned = 0;  // Number of coins returned in change from function below (for debug)
  int nNeededSpends = 0;   // Number of spends which would be needed if selection failed
  const int nMaxSpends =
      Params().Zerocoin_MaxSpendsPerTransaction();  // Maximum possible spends for one ZKP transaction
  vector<CMintMeta> vMintsToFetch;
  if (vSelectedMints.empty()) {
    setMints = zkpTracker->ListMints(true, true, true);  // need to find mints to spend
    if (setMints.empty()) {
      receipt.SetStatus(_("Failed to find Zerocoins in wallet.dat"), nStatus);
      return false;
    }

    // If the input value is not an int, then we want the selection algorithm to round up to the next highest int
    double dValue = static_cast<double>(nValue) / static_cast<double>(COIN);
    bool fWholeNumber = floor(dValue) == dValue;
    CAmount nValueToSelect = nValue;
    if (!fWholeNumber) nValueToSelect = static_cast<CAmount>(ceil(dValue) * COIN);

    // Select the ZKP mints to use in this spend
    std::map<libzerocoin::CoinDenomination, CAmount> DenomMap = GetMyZerocoinDistribution();
    list<CMintMeta> listMints(setMints.begin(), setMints.end());
    vMintsToFetch = SelectMintsFromList(nValueToSelect, nValueSelected, nMaxSpends, fMinimizeChange, nCoinsReturned,
                                        listMints, DenomMap, nNeededSpends);
    for (auto& meta : vMintsToFetch) {
      CZerocoinMint mint;
      if (!GetMint(meta.hashSerial, mint))
        return error("%s: failed to fetch hashSerial %s", __func__, meta.hashSerial.GetHex());
      vSelectedMints.emplace_back(mint);
    }
  } else {
    for (const CZerocoinMint mint : vSelectedMints)
      nValueSelected += ZerocoinDenominationToAmount(mint.GetDenomination());
  }

  int nArchived = 0;
  for (CZerocoinMint mint : vSelectedMints) {
    // see if this serial has already been spent
    int nHeightSpend;
    if (IsSerialInBlockchain(mint.GetSerialNumber(), nHeightSpend)) {
      receipt.SetStatus(_("Trying to spend an already spent serial #, try again."), nStatus);
      uint256 hashSerial = GetSerialHash(mint.GetSerialNumber());
      if (!zkpTracker->HasSerialHash(hashSerial))
        return error("%s: tracker does not have serialhash %s", __func__, hashSerial.GetHex());

      CMintMeta meta = zkpTracker->Get(hashSerial);
      meta.isUsed = true;
      zkpTracker->UpdateState(meta);

      return false;
    }

    // check that this mint made it into the blockchain
    CTransaction txMint;
    uint256 hashBlock;
    bool fArchive = false;
    if (!GetTransaction(mint.GetTxHash(), txMint, hashBlock)) {
      receipt.SetStatus(_("Unable to find transaction containing mint"), nStatus);
      fArchive = true;
    } else if (mapBlockIndex.count(hashBlock) < 1) {
      receipt.SetStatus(_("Mint did not make it into blockchain"), nStatus);
      fArchive = true;
    }

    // archive this mint as an orphan
    if (fArchive) {
      // walletdb.ArchiveMintOrphan(mint);
      // nArchived++;
      // todo
    }
  }
  if (nArchived) return false;

  if (vSelectedMints.empty()) {
    if (nNeededSpends > 0) {
      // Too much spends needed, so abuse nStatus to report back the number of needed spends
      receipt.SetStatus(_("Too many spends needed"), nStatus, nNeededSpends);
    } else {
      receipt.SetStatus(_("Failed to select a zerocoin"), nStatus);
    }
    return false;
  }

  if ((static_cast<int>(vSelectedMints.size()) > Params().Zerocoin_MaxSpendsPerTransaction())) {
    receipt.SetStatus(_("Failed to find coin set amongst held coins with less than maxNumber of Spends"), nStatus);
    return false;
  }

  // Create change if needed
  nStatus = ZKP_TRX_CHANGE;

  CMutableTransaction txNew;
  wtxNew.BindWallet(this);
  {
    LOCK2(cs_main, cs_wallet);
    {
      txNew.vin.clear();
      txNew.vout.clear();

      // if there is an address to send to then use it, if not generate a new address to send to
      CScript scriptZerocoinSpend;
      CScript scriptChange;
      CAmount nChange = nValueSelected - nValue;

      if (nChange < 0) {
        receipt.SetStatus(_("Selected coins value is less than payment target"), nStatus);
        return false;
      }

      if (nChange > 0 && !address) {
        receipt.SetStatus(_("Need address because change is not exact"), nStatus);
        return false;
      }

      if (address) {
        scriptZerocoinSpend = GetScriptForDestination(address->Get());
        if (nChange) {
          // Reserve a new key pair from key pool
          CPubKey vchPubKey;
          assert(reserveKey.GetReservedKey(vchPubKey));  // should never fail
          scriptChange = GetScriptForDestination(vchPubKey.GetID());
        }
      } else {
        // Reserve a new key pair from key pool
        CPubKey vchPubKey;
        assert(reserveKey.GetReservedKey(vchPubKey));  // should never fail
        scriptZerocoinSpend = GetScriptForDestination(vchPubKey.GetID());
      }

      // add change output if we are spending too much (only applies to spending multiple at once)
      if (nChange) {
        // mint change as zerocoins
        if (fMintChange) {
          CAmount nFeeRet = 0;
          string strFailReason = "";
          if (!CreateZerocoinMintTransaction(nChange, txNew, vNewMints, &reserveKey, nFeeRet, strFailReason, nullptr,
                                             true)) {
            receipt.SetStatus(_("Failed to create mint"), nStatus);
            return false;
          }
        } else {
          CTxOut txOutChange(nValueSelected - nValue, scriptChange);
          txNew.vout.push_back(txOutChange);
        }
      }

      // add output to club address to the transaction (the actual primary spend taking place)
      CTxOut txOutZerocoinSpend(nValue, scriptZerocoinSpend);
      txNew.vout.push_back(txOutZerocoinSpend);

      // hash with only the output info in it to be used in Signature of Knowledge
      uint256 hashTxOut = txNew.GetHash();

      // add all of the mints to the transaction as inputs
      for (CZerocoinMint mint : vSelectedMints) {
        CTxIn newTxIn;
        if (!MintToTxIn(mint, nSecurityLevel, hashTxOut, newTxIn, receipt, libzerocoin::SpendType::SPEND)) {
          return false;
        }
        txNew.vin.push_back(newTxIn);
      }

      // Limit size
      unsigned int nBytes = ::GetSerializeSize(txNew);
      if (nBytes >= MAX_ZEROCOIN_TX_SIZE) {
        receipt.SetStatus(_("In rare cases, a spend with 7 coins exceeds our maximum allowable transaction size, "
                            "please retry spend using 6 or less coins"),
                          ZKP_TX_TOO_LARGE);
        return false;
      }

      // now that all inputs have been added, add full tx hash to zerocoinspend records and write to db
      uint256 txHash = txNew.GetHash();
      for (CZerocoinSpend spend : receipt.GetSpends()) {
        spend.SetTxHash(txHash);

        if (!CWalletDB(strWalletFile).WriteZerocoinSpendSerialEntry(spend)) {
          receipt.SetStatus(_("Failed to write coin serial number into wallet"), nStatus);
        }
      }

      // turn the finalized transaction into a wallet transaction
      wtxNew = CWalletTx(this, txNew);
      wtxNew.fFromMe = true;
      wtxNew.fTimeReceivedIsTxTime = true;
      wtxNew.nTimeReceived = GetAdjustedTime();
    }
  }

  receipt.SetStatus(_("Transaction Created"), ZKP_SPEND_OKAY);  // Everything okay

  return true;
}

string CWallet::ResetMintZerocoin() {
  long updates = 0;
  long deletions = 0;
  CWalletDB walletdb(pwalletMain->strWalletFile);

  set<CMintMeta> setMints = zkpTracker->ListMints(false, false, true);
  vector<CMintMeta> vMintsToFind(setMints.begin(), setMints.end());
  vector<CMintMeta> vMintsMissing;
  vector<CMintMeta> vMintsToUpdate;

  // search all of our available data for these mints
  FindMints(vMintsToFind, vMintsToUpdate, vMintsMissing);

  // Update the meta data of mints that were marked for updating
  for (CMintMeta meta : vMintsToUpdate) {
    updates++;
    zkpTracker->UpdateState(meta);
  }

  // Delete any mints that were unable to be located on the blockchain
  for (CMintMeta mint : vMintsMissing) {
    deletions++;
    if (!zkpTracker->Archive(mint)) LogPrintf("%s: failed to archive mint\n", __func__);
  }

  NotifyZkpReset();

  string strResult = _("ResetMintZerocoin finished: ") + to_string(updates) + _(" mints updated, ") +
                     to_string(deletions) + _(" mints deleted\n");
  return strResult;
}

string CWallet::ResetSpentZerocoin() {
  long removed = 0;
  CWalletDB walletdb(pwalletMain->strWalletFile);

  set<CMintMeta> setMints = zkpTracker->ListMints(false, false, true);
  list<CZerocoinSpend> listSpends = walletdb.ListSpentCoins();
  list<CZerocoinSpend> listUnconfirmedSpends;

  for (CZerocoinSpend spend : listSpends) {
    CTransaction tx;
    uint256 hashBlock(uint256S("0"));
    if (!GetTransaction(spend.GetTxHash(), tx, hashBlock)) {
      listUnconfirmedSpends.push_back(spend);
      continue;
    }

    // no confirmations
    if (hashBlock.IsNull()) listUnconfirmedSpends.push_back(spend);
  }

  for (CZerocoinSpend spend : listUnconfirmedSpends) {
    for (CMintMeta meta : setMints) {
      if (meta.hashSerial == GetSerialHash(spend.GetSerial())) {
        removed++;
        meta.isUsed = false;
        zkpTracker->UpdateState(meta);
        walletdb.EraseZerocoinSpendSerialEntry(spend.GetSerial());
        continue;
      }
    }
  }

  NotifyZkpReset();

  string strResult = _("ResetSpentZerocoin finished: ") + to_string(removed) + _(" unconfirmed transactions removed\n");
  return strResult;
}

bool IsMintInChain(const uint256& hashPubcoin, uint256& txid, int& nHeight) {
  if (!IsPubcoinInBlockchain(hashPubcoin, txid)) return false;

  uint256 hashBlock;
  CTransaction tx;
  if (!GetTransaction(txid, tx, hashBlock)) return false;

  if (!mapBlockIndex.count(hashBlock) || !chainActive.Contains(mapBlockIndex.at(hashBlock))) return false;

  nHeight = mapBlockIndex.at(hashBlock)->nHeight;
  return true;
}

void CWallet::ReconsiderZerocoins(std::list<CZerocoinMint>& listMintsRestored,
                                  std::list<CDeterministicMint>& listDMintsRestored) {
  CWalletDB walletdb(pwalletMain->strWalletFile);
  list<CZerocoinMint> listMints = walletdb.ListArchivedZerocoins();
  list<CDeterministicMint> listDMints = walletdb.ListArchivedDeterministicMints();

  if (listMints.empty() && listDMints.empty()) return;

  for (CZerocoinMint mint : listMints) {
    uint256 txid;
    int nHeight;
    uint256 hashPubcoin = GetPubCoinHash(mint.GetValue());
    if (!IsMintInChain(hashPubcoin, txid, nHeight)) continue;

    mint.SetTxHash(txid);
    mint.SetHeight(nHeight);
    mint.SetUsed(IsSerialInBlockchain(mint.GetSerialNumber(), nHeight));

    if (!zkpTracker->UnArchive(hashPubcoin, false)) {
      LogPrintf("%s : failed to unarchive mint %s\n", __func__, mint.GetValue().GetHex());
    } else {
      zkpTracker->UpdateZerocoinMint(mint);
    }
    listMintsRestored.emplace_back(mint);
  }

  for (CDeterministicMint dMint : listDMints) {
    uint256 txid;
    int nHeight;
    if (!IsMintInChain(dMint.GetPubcoinHash(), txid, nHeight)) continue;

    dMint.SetTxHash(txid);
    dMint.SetHeight(nHeight);
    uint256 txidSpend;
    dMint.SetUsed(IsSerialInBlockchain(dMint.GetSerialHash(), nHeight, txidSpend));

    if (!zkpTracker->UnArchive(dMint.GetPubcoinHash(), true)) {
      LogPrintf("%s : failed to unarchive deterministic mint %s\n", __func__, dMint.GetPubcoinHash().GetHex());
    } else {
      zkpTracker->Add(dMint, true);
    }
    listDMintsRestored.emplace_back(dMint);
  }
}

string CWallet::GetUniqueWalletBackupName(bool fzkpAuto) const {
  boost::posix_time::ptime timeLocal = boost::posix_time::second_clock::local_time();
  stringstream ssDateTime;

  ssDateTime << boost::gregorian::to_iso_extended_string(timeLocal.date()) << "-" << timeLocal.time_of_day();
  return strprintf("wallet%s.dat%s", fzkpAuto ? "-autozkpbackup" : "", DateTimeStrFormat(".%Y-%m-%d-%H-%M", GetTime()));
}

void CWallet::ZkpBackupWallet() {
  fs::path backupDir = GetDataDir() / "backups";
  fs::path backupPath;
  string strNewBackupName;

  for (int i = 0; i < 10; i++) {
    strNewBackupName = strprintf("wallet-autozkpbackup-%d.dat", i);
    backupPath = backupDir / strNewBackupName;

    if (fs::exists(backupPath)) {
      // Keep up to 10 backups
      if (i <= 8) {
        // If the next file backup exists and is newer, then iterate
        fs::path nextBackupPath = backupDir / strprintf("wallet-autozkpbackup-%d.dat", i + 1);
        if (fs::exists(nextBackupPath)) {
          time_t timeThis = fs::last_write_time(backupPath);
          time_t timeNext = fs::last_write_time(nextBackupPath);
          if (timeThis > timeNext) {
            // The next backup is created before this backup was
            // The next backup is the correct path to use
            backupPath = nextBackupPath;
            break;
          }
        }
        // Iterate to the next filename/number
        continue;
      }
      // reset to 0 because name with 9 already used
      strNewBackupName = strprintf("wallet-autozkpbackup-%d.dat", 0);
      backupPath = backupDir / strNewBackupName;
      break;
    }
    // This filename is fresh, break here and backup
    break;
  }

  BackupWallet(*this, backupPath.string());

  if (!GetArg("-zkpbackuppath", "").empty()) {
    fs::path customPath(GetArg("-zkpbackuppath", ""));
    fs::create_directories(customPath);

    if (!customPath.has_extension()) { customPath /= GetUniqueWalletBackupName(true); }

    BackupWallet(*this, customPath, false);
  }
}

string CWallet::MintZerocoinFromOutPoint(CAmount nValue, CWalletTx& wtxNew, vector<CDeterministicMint>& vDMints,
                                         const vector<COutPoint> vOutpts) {
  CCoinControl* coinControl = new CCoinControl();
  for (const COutPoint& outpt : vOutpts) { coinControl->Select(outpt); }
  if (!coinControl->HasSelected()) {
    string strError = _("Error: No valid utxo!");
    LogPrintf("MintZerocoin() : %s", strError.c_str());
    return strError;
  }
  string strError = MintZerocoin(nValue, wtxNew, vDMints, coinControl);
  delete coinControl;
  return strError;
}

string CWallet::MintZerocoin(CAmount nValue, CWalletTx& wtxNew, vector<CDeterministicMint>& vDMints,
                             const CCoinControl* coinControl) {
  // Check amount
  if (nValue <= 0) return _("Invalid amount");

  if (nValue + Params().Zerocoin_MintFee() > GetBalance()) return _("Insufficient funds");

  CReserveKey reservekey(this);
  int64_t nFeeRequired;

  if (IsLocked()) {
    string strError = _("Error: Wallet locked, unable to create transaction!");
    LogPrintf("MintZerocoin() : %s", strError.c_str());
    return strError;
  }

  string strError;
  CMutableTransaction txNew;
  if (!CreateZerocoinMintTransaction(nValue, txNew, vDMints, &reservekey, nFeeRequired, strError, coinControl)) {
    if (nValue + nFeeRequired > GetBalance())
      return strprintf(_("Error: This transaction requires a transaction fee of at least %s because of its amount, "
                         "complexity, or use of recently received funds!"),
                       FormatMoney(nFeeRequired).c_str());
    return strError;
  }

  wtxNew = CWalletTx(this, txNew);
  wtxNew.fFromMe = true;
  wtxNew.fTimeReceivedIsTxTime = true;

  // Limit size
  unsigned int nBytes = ::GetSerializeSize(txNew);
  if (nBytes >= MAX_ZEROCOIN_TX_SIZE) {
    return _("Error: The transaction is larger than the maximum allowed transaction size!");
  }

  // commit the transaction to the network
  if (!CommitTransaction(wtxNew, reservekey)) {
    return _(
        "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already "
        "spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent "
        "here.");
  } else {
    // update mints with full transaction hash and then database them
    CWalletDB walletdb(pwalletMain->strWalletFile);
    for (CDeterministicMint dMint : vDMints) {
      dMint.SetTxHash(wtxNew.GetHash());
      zkpTracker->Add(dMint, true);
    }
  }

  // Create a backup of the wallet
  if (fBackupMints) ZkpBackupWallet();

  return "";
}

bool CWallet::SpendZerocoin(CAmount nAmount, int nSecurityLevel, CWalletTx& wtxNew, CZerocoinSpendReceipt& receipt,
                            vector<CZerocoinMint>& vMintsSelected, bool fMintChange, bool fMinimizeChange,
                            CBitcoinAddress* addressTo) {
  // Default: assume something goes wrong. Depending on the problem this gets more specific below
  int nStatus = ZKP_SPEND_ERROR;

  if (IsLocked()) {
    receipt.SetStatus("Error: Wallet locked, unable to create transaction!", ZKP_WALLET_LOCKED);
    return false;
  }

  CReserveKey reserveKey(this);
  vector<CDeterministicMint> vNewMints;
  if (!CreateZerocoinSpendTransaction(nAmount, nSecurityLevel, wtxNew, reserveKey, receipt, vMintsSelected, vNewMints,
                                      fMintChange, fMinimizeChange, addressTo)) {
    return false;
  }

  if (fMintChange && fBackupMints) ZkpBackupWallet();

  CWalletDB walletdb(pwalletMain->strWalletFile);
  if (!CommitTransaction(wtxNew, reserveKey)) {
    LogPrintf("%s: failed to commit\n", __func__);
    nStatus = ZKP_COMMIT_FAILED;

    // reset all mints
    for (CZerocoinMint mint : vMintsSelected) {
      uint256 hashPubcoin = GetPubCoinHash(mint.GetValue());
      zkpTracker->SetPubcoinNotUsed(hashPubcoin);
      pwalletMain->NotifyZerocoinChanged(pwalletMain, mint.GetValue().GetHex(), "New", CT_UPDATED);
    }

    // erase spends
    for (CZerocoinSpend spend : receipt.GetSpends()) {
      if (!walletdb.EraseZerocoinSpendSerialEntry(spend.GetSerial())) {
        receipt.SetStatus("Error: It cannot delete coin serial number in wallet", ZKP_ERASE_SPENDS_FAILED);
      }

      // Remove from public zerocoinDB
      RemoveSerialFromDB(spend.GetSerial());
    }

    // erase new mints
    for (auto& dMint : vNewMints) {
      if (!walletdb.EraseDeterministicMint(dMint.GetPubcoinHash())) {
        receipt.SetStatus("Error: Unable to cannot delete zerocoin mint in wallet", ZKP_ERASE_NEW_MINTS_FAILED);
      }
    }

    receipt.SetStatus(
        "Error: The transaction was rejected! This might happen if some of the coins in your wallet were already "
        "spent, such as if you used a copy of wallet.dat and coins were spent in the copy but not marked as spent "
        "here.",
        nStatus);
    return false;
  }

  // Set spent mints as used
  uint256 txidSpend = wtxNew.GetHash();
  for (CZerocoinMint mint : vMintsSelected) {
    uint256 hashPubcoin = GetPubCoinHash(mint.GetValue());
    zkpTracker->SetPubcoinUsed(hashPubcoin, txidSpend);

    CMintMeta metaCheck = zkpTracker->GetMetaFromPubcoin(hashPubcoin);
    if (!metaCheck.isUsed) {
      receipt.SetStatus("Error, the mint did not get marked as used", nStatus);
      return false;
    }
  }

  // write new Mints to db
  for (auto& dMint : vNewMints) {
    dMint.SetTxHash(txidSpend);
    zkpTracker->Add(dMint, true);
  }

  receipt.SetStatus("Spend Successful", ZKP_SPEND_OKAY);  // When we reach this point spending ZKP was successful

  return true;
}

bool CWallet::GetMint(const uint256& hashSerial, CZerocoinMint& mint) {
  if (!zkpTracker->HasSerialHash(hashSerial))
    return error("%s: serialhash %s is not in tracker", __func__, hashSerial.GetHex());

  CWalletDB walletdb(strWalletFile);
  CMintMeta meta = zkpTracker->Get(hashSerial);
  if (meta.isDeterministic) {
    CDeterministicMint dMint;
    if (!walletdb.ReadDeterministicMint(meta.hashPubcoin, dMint))
      return error("%s: failed to read deterministic mint", __func__);
    if (!zwalletMain->RegenerateMint(dMint, mint)) return error("%s: failed to generate mint", __func__);

    return true;
  } else if (!walletdb.ReadZerocoinMint(meta.hashPubcoin, mint)) {
    return error("%s: failed to read zerocoinmint from database", __func__);
  }

  return true;
}

bool CWallet::IsMyMint(const CBigNum& bnValue) const {
  if (zkpTracker->HasPubcoin(bnValue)) return true;

  return zwalletMain->IsInMintPool(bnValue);
}

bool CWallet::UpdateMint(const CBigNum& bnValue, const int& nHeight, const uint256& txid,
                         const libzerocoin::CoinDenomination& denom) {
  uint256 hashValue = GetPubCoinHash(bnValue);
  CZerocoinMint mint;
  if (zkpTracker->HasPubcoinHash(hashValue)) {
    CMintMeta meta = zkpTracker->GetMetaFromPubcoin(hashValue);
    meta.nHeight = nHeight;
    meta.txid = txid;
    return zkpTracker->UpdateState(meta);
  } else {
    // Check if this mint is one that is in our mintpool (a potential future mint from our deterministic generation)
    if (zwalletMain->IsInMintPool(bnValue)) {
      if (zwalletMain->SetMintSeen(bnValue, nHeight, txid, denom)) return true;
    }
  }

  return false;
}

//! Primarily for the scenario that a mint was confirmed and added to the chain and then that block orphaned
bool CWallet::SetMintUnspent(const CBigNum& bnSerial) {
  uint256 hashSerial = GetSerialHash(bnSerial);
  if (!zkpTracker->HasSerialHash(hashSerial)) return error("%s: did not find mint", __func__);

  CMintMeta meta = zkpTracker->Get(hashSerial);
  zkpTracker->SetPubcoinNotUsed(meta.hashPubcoin);
  return true;
}

bool CWallet::DatabaseMint(CDeterministicMint& dMint) {
  CWalletDB walletdb(strWalletFile);
  zkpTracker->Add(dMint, true);
  return true;
}
