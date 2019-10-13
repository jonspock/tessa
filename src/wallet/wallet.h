// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "amount.h"
#include "key_io.h"
#include "crypter.h"

#include "account.h"
#include "addressbookdata.h"
#include "chainparams.h"
#include "libzerocoin/key.h"
#include "bls/key.h"
#include "keypool.h"
#include "keystore.h"
#include "main_functions.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "reservekey.h"
#include "stake.h"
#include "txmempool.h"
#include "ui_interface.h"
#include "util.h"
#include "validationinterface.h"
#include "wallet_functions.h"
#include "wallet_ismine.h"
#include "walletdb.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "signals-cpp/signals.hpp"

/**
 * Settings
 */
extern uint32_t nTxConfirmTarget;
extern bool bSpendZeroConfChange;
extern bool bdisableSystemnotifications;
extern bool fSendFreeTransactions;
extern bool fPayAtLeastCustomFee;

static const CAmount DEFAULT_TRANSACTION_FEE = 0;
//! Largest (in bytes) free transaction we're willing to create
static const uint32_t MAX_FREE_TRANSACTION_CREATE_SIZE = 1000;
//! -custombackupthreshold default
static const int DEFAULT_CUSTOMBACKUPTHRESHOLD = 1;

//! if set, all keys will be derived by using BIP32
static const bool DEFAULT_USE_HD_WALLET = true;

class CCoinControl;
class COutput;
class CScript;
class CWalletTx;

/** (client) version numbers for particular wallet features */
enum WalletFeature { FEATURE_LATEST = 90000 };

enum AvailableCoinsType {
  ALL_COINS = 1,
  STAKABLE_COINS = 2  // UTXO's that are valid for staking
};

int64_t getReserveBalance();
void setReserveBalance(int64_t b);

/**
 * A CWallet is an extension of a keystore, which also maintains a set of transactions and balances,
 * and provides the ability to create new transactions.
 */
class CWallet : public CCryptoKeyStore, public CValidationInterface {
 private:
  bool SelectCoins(const CAmount& nTargetValue, std::set<std::pair<const CWalletTx*, uint32_t> >& setCoinsRet,
                   CAmount& nValueRet, const CCoinControl* coinControl = nullptr,
                   AvailableCoinsType coin_type = ALL_COINS, bool useIX = true) const;
  // it was public bool SelectCoins(int64_t nTargetValue, std::set<std::pair<const CWalletTx*,uint32_t> >&
  // setCoinsRet, int64_t& nValueRet, const CCoinControl *coinControl = nullptr, AvailableCoinsType coin_type=ALL_COINS,
  // bool useIX = true) const;

  //! the current wallet version: clients below this version are not able to load the wallet
  int nWalletVersion;

  //! the maximum wallet format version: memory-only variable that specifies to what version this wallet may be upgraded
  int nWalletMaxVersion;

  int64_t nNextResend;
  int64_t nLastResend;

  /**
   * Used to keep track of spent outpoints, and
   * detect and report conflicts (double-spends or
   * mutated transactions where the mutant gets mined).
   */
  typedef std::multimap<COutPoint, uint256> TxSpends;
  TxSpends mapTxSpends;
  void AddToSpends(const COutPoint& outpoint, const uint256& wtxid);
  void AddToSpends(const uint256& wtxid);

  void SyncMetaData(std::pair<TxSpends::iterator, TxSpends::iterator>);

 public:
  bool MintableCoins();
  bool SelectStakeCoins(std::list<std::unique_ptr<CStake> >& listInputs, CAmount nTargetAmount);
  int CountInputsWithAmount(CAmount nInputAmount);

  // Zerocoin additions

  std::string GetUniqueWalletBackupName(bool fzkpAuto) const;

  /*
   * Main wallet lock.
   * This lock protects all the fields added by CWallet
   *   except for:
   *      fFileBacked (immutable after instantiation)
   *      strWalletFile (immutable after instantiation)
   */
  mutable CCriticalSection cs_wallet;

  bool fFileBacked;
  bool fWalletUnlockAnonymizeOnly;
  bool fBackupMints;

  /* the HD chain data model (external chain counters) */
  CHDChain hdChain;

  /* HD derive new child key (on internal or external chain) */
  void DeriveNewChildKey(CKeyMetadata& metadata, bls::CKey& secret, bool internal = false);
  
  std::set<int64_t> setKeyPool;
  std::map<bls::CKeyID, CKeyMetadata> mapKeyMetadata;

  typedef std::map<uint32_t, CMasterKey> MasterKeyMap;
  MasterKeyMap mapMasterKeys;
  uint32_t nMasterKeyMaxID;

  // Stake Settings
  uint32_t nHashDrift;
  uint32_t nHashInterval;
  uint64_t nStakeSplitThreshold;
  int nStakeSetUpdateTime;

  // MultiSend
  std::vector<std::pair<std::string, int> > vMultiSend;
  bool fMultiSendStake;
  bool fMultiSendMasternodeReward;
  bool fMultiSendNotify;
  std::string strMultiSendChangeAddress;
  int nLastMultiSendHeight;
  std::vector<std::string> vDisabledAddresses;

  // Auto Combine Inputs
  bool fCombineDust;
  CAmount nAutoCombineThreshold;

  CWallet() {
    SetNull();
    fFileBacked = true;
  }

  ~CWallet() {}

  void SetNull() {
    nWalletVersion = FEATURE_LATEST;
    nWalletMaxVersion = FEATURE_LATEST;
    fFileBacked = false;
    nMasterKeyMaxID = 0;
    nOrderPosNext = 0;
    nNextResend = 0;
    nLastResend = 0;
    nTimeFirstKey = 0;
    fWalletUnlockAnonymizeOnly = false;
    fBackupMints = false;

    // Stake Settings
    nHashDrift = 45;
    nStakeSplitThreshold = 2000;
    nHashInterval = 22;
    nStakeSetUpdateTime = 300;  // 5 minutes

    // MultiSend
    vMultiSend.clear();
    fMultiSendStake = false;
    fMultiSendMasternodeReward = false;
    fMultiSendNotify = false;
    strMultiSendChangeAddress = "";
    nLastMultiSendHeight = 0;
    vDisabledAddresses.clear();

    // Auto Combine Dust
    fCombineDust = false;
    nAutoCombineThreshold = 0;
  }

  bool isMultiSendEnabled() { return fMultiSendMasternodeReward || fMultiSendStake; }

  void setMultiSendDisabled() {
    fMultiSendMasternodeReward = false;
    fMultiSendStake = false;
  }

  std::map<uint256, CWalletTx> mapWallet;
  std::list<CAccountingEntry> laccentries;

  typedef std::pair<CWalletTx*, CAccountingEntry*> TxPair;
  typedef std::multimap<int64_t, TxPair> TxItems;
  TxItems wtxOrdered;

  int64_t nOrderPosNext;
  std::map<uint256, int> mapRequestCount;

  std::map<CTxDestination, CAddressBookData> mapAddressBook;

  bls::CPubKey vchDefaultKey;

  std::set<COutPoint> setLockedCoins;

  int64_t nTimeFirstKey;

  const CWalletTx* GetWalletTx(const uint256& hash) const;

  //! check whether we are allowed to upgrade (or already support) to the named feature
  bool CanSupportFeature(enum WalletFeature wf) {
    AssertLockHeld(cs_wallet);
    return nWalletMaxVersion >= wf;
  }

  void AvailableCoins(std::vector<COutput>& vCoins, bool fOnlyConfirmed = true,
                      const CCoinControl* coinControl = nullptr, bool fIncludeZeroValue = false,
                      AvailableCoinsType nCoinType = ALL_COINS, bool fUseIX = false, int nWatchonlyConfig = 1) const;
  std::map<CTxDestination, std::vector<COutput> > AvailableCoinsByAddress(bool fConfirmed = true,
                                                                           CAmount maxCoinValue = 0);
  bool SelectCoinsMinConf(const CAmount& nTargetValue, int nConfMine, int nConfTheirs, std::vector<COutput> vCoins,
                          std::set<std::pair<const CWalletTx*, uint32_t> >& setCoinsRet, CAmount& nValueRet) const;

  bool IsSpent(const uint256& hash, uint32_t n) const;
  bool IsLockedCoin(uint256 hash, uint32_t n) const;
  void LockCoin(COutPoint& output);
  void UnlockCoin(COutPoint& output);
  void UnlockAllCoins();
  void ListLockedCoins(std::vector<COutPoint>& vOutpts);
  CAmount GetTotalValue(std::vector<CTxIn> vCoins);

  //  keystore implementation
  // Generate a new key
  bls::CPubKey GenerateNewKey();

  //! Adds a key to the store, and saves it to disk.
  bool AddKeyPubKey(const bls::CKey& key, const bls::CPubKey& pubkey);
  bool AddKeyPubKeyWithDB(const bls::CKey& key, const bls::CPubKey& pubkey);
  //! Adds a key to the store, without saving it to disk (used by LoadWallet)
  bool LoadKey(const bls::CKey& key, const bls::CPubKey& pubkey) {
    return CCryptoKeyStore::AddKeyPubKey(key, pubkey);
  }
  //! Load metadata (used by LoadWallet)
  bool LoadKeyMetadata(const bls::CPubKey& pubkey, const CKeyMetadata& metadata);

  bool LoadMinVersion(int nVersion) {
    AssertLockHeld(cs_wallet);
    nWalletVersion = nVersion;
    nWalletMaxVersion = std::max(nWalletMaxVersion, nVersion);
    return true;
  }

  //! Adds an encrypted key to the store, and saves it to disk.
  bool AddCryptedKey(const bls::CPubKey& vchPubKey, const std::vector<uint8_t>& vchCryptedSecret);
  //! Adds an encrypted key to the store, without saving it to disk (used by LoadWallet)
  bool LoadCryptedKey(const bls::CPubKey& vchPubKey, const std::vector<uint8_t>& vchCryptedSecret);
  bool AddCScript(const CScript& redeemScript);
  bool LoadCScript(const CScript& redeemScript);

  //! Adds a destination data tuple to the store, and saves it to disk
  bool AddDestData(const CTxDestination& dest, const std::string& key, const std::string& value);
  //! Erases a destination data tuple in the store and on disk
  bool EraseDestData(const CTxDestination& dest, const std::string& key);
  //! Adds a destination data tuple to the store, without saving it to disk
  bool LoadDestData(const CTxDestination& dest, const std::string& key, const std::string& value);
  //! Look up a destination data tuple in the store, return true if found false otherwise
  bool GetDestData(const CTxDestination& dest, const std::string& key, std::string* value) const;

  //! Adds a watch-only address to the store, and saves it to disk.
  bool AddWatchOnly(const CScript& dest);
  bool RemoveWatchOnly(const CScript& dest);
  //! Adds a watch-only address to the store, without saving it to disk (used by LoadWallet)
  bool LoadWatchOnly(const CScript& dest);

  //! Adds a MultiSig address to the store, and saves it to disk.
  bool AddMultiSig(const CScript& dest);
  bool RemoveMultiSig(const CScript& dest);
  //! Adds a MultiSig address to the store, without saving it to disk (used by LoadWallet)
  bool LoadMultiSig(const CScript& dest);

  bool Unlock(const SecureString& strWalletPassphrase, bool anonimizeOnly = false);
  void SetMaster(const CKeyingMaterial& vInMasterKey);
  bool ChangeWalletPassphrase(const SecureString& strOldWalletPassphrase, const SecureString& strNewWalletPassphrase);
  bool SetupCrypter(const SecureString& strWalletPassphrase);

  void GetKeyBirthTimes(std::map<bls::CKeyID, int64_t>& mapKeyBirth) const;
  uint32_t ComputeTimeSmart(const CWalletTx& wtx) const;

  /**
   * Increment the next transaction order id
   * @return next transaction order id
   */
  int64_t IncOrderPosNext();

  void MarkDirty();
  bool AddToWallet(const CWalletTx& wtxIn, bool fFromLoadWallet = false);
  void SyncTransaction(const CTransaction& tx, const CBlock* pblock);
  bool AddToWalletIfInvolvingMe(const CTransaction& tx, const CBlock* pblock, bool fUpdate);
  void EraseFromWallet(const uint256& hash);
  int ScanForWalletTransactions(CBlockIndex* pindexStart, bool fUpdate = false);
  void ReacceptWalletTransactions();
  void ResendWalletTransactions();
  CAmount GetBalance() const;
  CAmount GetLockedCoins() const;
  CAmount GetUnlockedCoins() const;
  CAmount GetUnconfirmedBalance() const;
  CAmount GetImmatureBalance() const;
  CAmount GetWatchOnlyBalance() const;
  CAmount GetUnconfirmedWatchOnlyBalance() const;
  CAmount GetImmatureWatchOnlyBalance() const;
  CAmount GetLockedWatchOnlyBalance() const;
  bool CreateTransaction(CScript scriptPubKey, int64_t nValue, CWalletTx& wtxNew, CReserveKey& reservekey,
                         int64_t& nFeeRet, std::string& strFailReason, const CCoinControl* coinControl);
  bool CreateTransaction(const std::vector<std::pair<CScript, CAmount> >& vecSend, CWalletTx& wtxNew,
                         CReserveKey& reservekey, CAmount& nFeeRet, std::string& strFailReason,
                         const CCoinControl* coinControl = nullptr, AvailableCoinsType coin_type = ALL_COINS,
                         bool useIX = false, CAmount nFeePay = 0);
  bool CreateTransaction(CScript scriptPubKey, const CAmount& nValue, CWalletTx& wtxNew, CReserveKey& reservekey,
                         CAmount& nFeeRet, std::string& strFailReason, const CCoinControl* coinControl = nullptr,
                         AvailableCoinsType coin_type = ALL_COINS, bool useIX = false, CAmount nFeePay = 0);
  bool CommitTransaction(CWalletTx& wtxNew, CReserveKey& reservekey, std::string strCommand = "tx");
  bool AddAccountingEntry(const CAccountingEntry& c);
  bool ConvertList(std::vector<CTxIn> vCoins, std::vector<int64_t>& vecAmounts);
  bool CreateCoinStake(const CKeyStore& keystore, uint32_t nBits, int64_t nSearchInterval, CMutableTransaction& txNew,
                       uint32_t& nTxNewTime);
  bool MultiSend();
  void AutoCombineDust();

  bool NewKeyPool();
  bool TopUpKeyPool(uint32_t kpSize = 0);
  void ReserveKeyFromKeyPool(int64_t& nIndex, CKeyPool& keypool);
  void KeepKey(int64_t nIndex);
  void ReturnKey(int64_t nIndex);
  bool GetKeyFromPool(bls::CPubKey& key);
  int64_t GetOldestKeyPoolTime();
  void GetAllReserveKeys(std::set<bls::CKeyID>& setAddress) const;

  std::set<std::set<CTxDestination> > GetAddressGroupings();
  std::map<CTxDestination, CAmount> GetAddressBalances();

  std::set<CTxDestination> GetAccountAddresses(const std::string& strAccount) const;

  isminetype IsMine(const CTxIn& txin) const;
  CAmount GetDebit(const CTxIn& txin, const isminefilter& filter) const;
  isminetype IsMine(const CTxOut& txout) const { return ::IsMine(*this, txout.scriptPubKey); }
  
  CAmount GetCredit(const CTxOut& txout, const isminefilter& filter) const {
    if (!MoneyRange(txout.nValue)) throw std::runtime_error("CWallet::GetCredit() : value out of range");
    return ((IsMine(txout) & filter) ? txout.nValue : 0);
  }
  bool IsChange(const CTxOut& txout) const;
  CAmount GetChange(const CTxOut& txout) const {
    if (!MoneyRange(txout.nValue)) throw std::runtime_error("CWallet::GetChange() : value out of range");
    return (IsChange(txout) ? txout.nValue : 0);
  }
  bool IsMine(const CTransaction& tx) const {
    for (const CTxOut& txout : tx.vout)
      if (IsMine(txout)) return true;
    return false;
  }
  /** should probably be renamed to IsRelevantToMe */
  bool IsFromMe(const CTransaction& tx) const { return (GetDebit(tx, ISMINE_ALL) > 0); }
  CAmount GetDebit(const CTransaction& tx, const isminefilter& filter) const {
    CAmount nDebit = 0;
    for (const CTxIn& txin : tx.vin) {
      nDebit += GetDebit(txin, filter);
      if (!MoneyRange(nDebit)) throw std::runtime_error("CWallet::GetDebit() : value out of range");
    }
    return nDebit;
  }
  CAmount GetCredit(const CTransaction& tx, const isminefilter& filter) const {
    CAmount nCredit = 0;
    for (const CTxOut& txout : tx.vout) {
      nCredit += GetCredit(txout, filter);
      if (!MoneyRange(nCredit)) throw std::runtime_error("CWallet::GetCredit() : value out of range");
    }
    return nCredit;
  }
  CAmount GetChange(const CTransaction& tx) const {
    CAmount nChange = 0;
    for (const CTxOut& txout : tx.vout) {
      nChange += GetChange(txout);
      if (!MoneyRange(nChange)) throw std::runtime_error("CWallet::GetChange() : value out of range");
    }
    return nChange;
  }
  void SetBestChain(const CBlockLocator& loc);

  DBErrors LoadWallet(bool& fFirstRunRet);
  DBErrors ZapWalletTx(std::vector<CWalletTx>& vWtx);

  bool SetAddressBook(const CTxDestination& address, const std::string& strName, const std::string& purpose);

  bool DelAddressBook(const CTxDestination& address);

  bool UpdatedTransaction(const uint256& hashTx);

  void Inventory(const uint256& hash) {
    {
      LOCK(cs_wallet);
      std::map<uint256, int>::iterator mi = mapRequestCount.find(hash);
      if (mi != mapRequestCount.end()) (*mi).second++;
    }
  }

  uint32_t GetKeyPoolSize() {
    AssertLockHeld(cs_wallet);  // setKeyPool
    return setKeyPool.size();
  }

  bool SetDefaultKey(const bls::CPubKey& vchPubKey);

  /* Set the HD chain model (chain child index counters) */
  bool SetHDChain(const CHDChain& chain, bool memonly);
  const CHDChain& GetHDChain() { return hdChain; }

  /* Returns true if HD is enabled */
  bool IsHDEnabled();

  /* Generates a new HD master key (will not be activated) */
  bls::CPubKey GenerateNewHDMasterKey();
  uint256 GetHDMasterKeySeed();  // returns HDMasterKey as uint256 for ZKP
  bool SetHDMasterKeyFromSeed(const uint256 seed);

  /* Set the current HD master key (will reset the chain child index counters)
   */
  bool SetHDMasterKey(const bls::CPubKey& key);

  //! get the current wallet format (the oldest client version guaranteed to understand this wallet)
  int GetVersion() {
    LOCK(cs_wallet);
    return nWalletVersion;
  }

  //! Get wallet transactions that conflict with given transaction (spend same outputs)
  std::set<uint256> GetConflicts(const uint256& txid) const;

  /**
   * Address book entry changed.
   * @note called with lock cs_wallet held.
   */
  sigs::signal<void(CWallet* wallet, const CTxDestination& address, const std::string& label, bool isMine,
                               const std::string& purpose, ChangeType status)>
      NotifyAddressBookChanged;

  /**
   * Wallet transaction added, removed or updated.
   * @note called with lock cs_wallet held.
   */
  sigs::signal<void(CWallet* wallet, const uint256& hashTx, ChangeType status)> NotifyTransactionChanged;

  /** Show progress e.g. for rescan */
  sigs::signal<void(const std::string& title, int nProgress)> ShowProgress;

  /** Watch-only address added */
  sigs::signal<void(bool fHaveWatchOnly)> NotifyWatchonlyChanged;

  /** MultiSig address added */
  sigs::signal<void(bool fHaveMultiSig)> NotifyMultiSigChanged;

  /** ZKP reset */
  sigs::signal<void()> NotifyZkpReset;

  /** notify wallet file backed up */
  sigs::signal<void(const bool& fSuccess, const std::string& filename)> NotifyWalletBacked;
};
