// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "amount.h"
#include "base58.h"
#include "crypter.h"

#include "account.h"
#include "addressbookdata.h"
#include "ecdsa/key.h"
#include "keypool.h"
#include "keystore.h"
#include "main_functions.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "primitives/zerocoin.h"
#include "reservekey.h"
#include "stakeinput.h"
#include "txmempool.h"
#include "ui_interface.h"
#include "util.h"
#include "validationinterface.h"
#include "wallet_functions.h"
#include "wallet_ismine.h"
#include "walletdb.h"
#include "zerotracker.h"
#include "zerowallet.h"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

/**
 * Settings
 */
extern unsigned int nTxConfirmTarget;
extern bool bSpendZeroConfChange;
extern bool bdisableSystemnotifications;
extern bool fSendFreeTransactions;
extern bool fPayAtLeastCustomFee;

static const CAmount DEFAULT_TRANSACTION_FEE = 0;
//! Largest (in bytes) free transaction we're willing to create
static const unsigned int MAX_FREE_TRANSACTION_CREATE_SIZE = 1000;
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

/**
 * A CWallet is an extension of a keystore, which also maintains a set of transactions and balances,
 * and provides the ability to create new transactions.
 */
class CWallet : public CCryptoKeyStore, public CValidationInterface {
 private:
  bool SelectCoins(const CAmount& nTargetValue, std::set<std::pair<const CWalletTx*, unsigned int> >& setCoinsRet,
                   CAmount& nValueRet, const CCoinControl* coinControl = nullptr,
                   AvailableCoinsType coin_type = ALL_COINS, bool useIX = true) const;
  // it was public bool SelectCoins(int64_t nTargetValue, std::set<std::pair<const CWalletTx*,unsigned int> >&
  // setCoinsRet, int64_t& nValueRet, const CCoinControl *coinControl = nullptr, AvailableCoinsType coin_type=ALL_COINS,
  // bool useIX = true) const;

  CWalletDB* pwalletdbEncryption;

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
  bool SelectStakeCoins(std::list<std::unique_ptr<CStakeInput> >& listInputs, CAmount nTargetAmount);
  int CountInputsWithAmount(CAmount nInputAmount);

  // Zerocoin additions
  bool CreateZerocoinMintTransaction(const CAmount nValue, CMutableTransaction& txNew,
                                     std::vector<CDeterministicMint>& vDMints, CReserveKey* reservekey,
                                     int64_t& nFeeRet, std::string& strFailReason,
                                     const CCoinControl* coinControl = nullptr, const bool isZCSpendChange = false);
  bool CreateZerocoinSpendTransaction(CAmount nValue, int nSecurityLevel, CWalletTx& wtxNew, CReserveKey& reserveKey,
                                      CZerocoinSpendReceipt& receipt, std::vector<CZerocoinMint>& vSelectedMints,
                                      std::vector<CDeterministicMint>& vNewMints, bool fMintChange,
                                      bool fMinimizeChange, CBitcoinAddress* address = nullptr);
  bool MintToTxIn(const CZerocoinMint& zerocoinSelected, int nSecurityLevel, const uint256& hashTxOut, CTxIn& newTxIn,
                  CZerocoinSpendReceipt& receipt, libzerocoin::SpendType spendType,
                  CBlockIndex* pindexCheckpoint = nullptr);
  std::string MintZerocoinFromOutPoint(CAmount nValue, CWalletTx& wtxNew, std::vector<CDeterministicMint>& vDMints,
                                       const std::vector<COutPoint> vOutpts);
  std::string MintZerocoin(CAmount nValue, CWalletTx& wtxNew, std::vector<CDeterministicMint>& vDMints,
                           const CCoinControl* coinControl = nullptr);
  bool SpendZerocoin(CAmount nValue, int nSecurityLevel, CWalletTx& wtxNew, CZerocoinSpendReceipt& receipt,
                     std::vector<CZerocoinMint>& vMintsSelected, bool fMintChange, bool fMinimizeChange,
                     CBitcoinAddress* addressTo = nullptr);
  std::string ResetMintZerocoin();
  std::string ResetSpentZerocoin();
  bool GetZerocoinKey(const CBigNum& bnSerial, ecdsa::CKey& key);
  bool CreateZKPOutPut(libzerocoin::CoinDenomination denomination, CTxOut& outMint, CDeterministicMint& dMint);
  bool GetMint(const uint256& hashSerial, CZerocoinMint& mint);
  bool SetMintUnspent(const CBigNum& bnSerial);
  bool UpdateMint(const CBigNum& bnValue, const int& nHeight, const uint256& txid,
                  const libzerocoin::CoinDenomination& denom);
  std::string GetUniqueWalletBackupName(bool fzkpAuto) const;

  /** Zerocin entry changed.
   * @note called with lock cs_wallet held.
   */
  boost::signals2::signal<void(CWallet* wallet, const std::string& pubCoin, const std::string& isUsed,
                               ChangeType status)>
      NotifyZerocoinChanged;
  /*
   * Main wallet lock.
   * This lock protects all the fields added by CWallet
   *   except for:
   *      fFileBacked (immutable after instantiation)
   *      strWalletFile (immutable after instantiation)
   */
  mutable CCriticalSection cs_wallet;

  CZeroWallet* zwalletMain;

  bool fFileBacked;
  bool fWalletUnlockAnonymizeOnly;
  bool fBackupMints;

  /* the HD chain data model (external chain counters) */
  CHDChain hdChain;

  /* HD derive new child key (on internal or external chain) */
  void DeriveNewChildKey(CWalletDB& walletdb, CKeyMetadata& metadata, ecdsa::CKey& secret, bool internal = false);

  std::unique_ptr<CZeroTracker> zkpTracker;

  std::set<int64_t> setKeyPool;
  std::map<ecdsa::CKeyID, CKeyMetadata> mapKeyMetadata;

  typedef std::map<unsigned int, CMasterKey> MasterKeyMap;
  MasterKeyMap mapMasterKeys;
  unsigned int nMasterKeyMaxID;

  // Stake Settings
  unsigned int nHashDrift;
  unsigned int nHashInterval;
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

  ~CWallet() { delete pwalletdbEncryption; }

  void SetNull() {
    nWalletVersion = FEATURE_LATEST;
    nWalletMaxVersion = FEATURE_LATEST;
    fFileBacked = false;
    nMasterKeyMaxID = 0;
    pwalletdbEncryption = nullptr;
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

  void setZWallet(CZeroWallet* zwallet) {
    zwalletMain = zwallet;
    zkpTracker = std::unique_ptr<CZeroTracker>(new CZeroTracker());
  }

  CZeroWallet* getZWallet() { return zwalletMain; }

  void setZkpAutoBackups(bool fEnabled) { fBackupMints = fEnabled; }

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

  ecdsa::CPubKey vchDefaultKey;

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
  std::map<CBitcoinAddress, std::vector<COutput> > AvailableCoinsByAddress(bool fConfirmed = true,
                                                                           CAmount maxCoinValue = 0);
  bool SelectCoinsMinConf(const CAmount& nTargetValue, int nConfMine, int nConfTheirs, std::vector<COutput> vCoins,
                          std::set<std::pair<const CWalletTx*, unsigned int> >& setCoinsRet, CAmount& nValueRet) const;

  bool IsSpent(const uint256& hash, unsigned int n) const;
  bool IsLockedCoin(uint256 hash, unsigned int n) const;
  void LockCoin(COutPoint& output);
  void UnlockCoin(COutPoint& output);
  void UnlockAllCoins();
  void ListLockedCoins(std::vector<COutPoint>& vOutpts);
  CAmount GetTotalValue(std::vector<CTxIn> vCoins);

  //  keystore implementation
  // Generate a new key
  ecdsa::CPubKey GenerateNewKey();

  //! Adds a key to the store, and saves it to disk.
  bool AddKeyPubKey(const ecdsa::CKey& key, const ecdsa::CPubKey& pubkey);
  bool AddKeyPubKeyWithDB(CWalletDB& walletdb, const ecdsa::CKey& key, const ecdsa::CPubKey& pubkey);
  //! Adds a key to the store, without saving it to disk (used by LoadWallet)
  bool LoadKey(const ecdsa::CKey& key, const ecdsa::CPubKey& pubkey) { return CCryptoKeyStore::AddKeyPubKey(key, pubkey); }
  //! Load metadata (used by LoadWallet)
  bool LoadKeyMetadata(const ecdsa::CPubKey& pubkey, const CKeyMetadata& metadata);

  bool LoadMinVersion(int nVersion) {
    AssertLockHeld(cs_wallet);
    nWalletVersion = nVersion;
    nWalletMaxVersion = std::max(nWalletMaxVersion, nVersion);
    return true;
  }

  //! Adds an encrypted key to the store, and saves it to disk.
  bool AddCryptedKey(const ecdsa::CPubKey& vchPubKey, const std::vector<uint8_t>& vchCryptedSecret);
  //! Adds an encrypted key to the store, without saving it to disk (used by LoadWallet)
  bool LoadCryptedKey(const ecdsa::CPubKey& vchPubKey, const std::vector<uint8_t>& vchCryptedSecret);
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
  bool ChangeWalletPassphrase(const SecureString& strOldWalletPassphrase, const SecureString& strNewWalletPassphrase);
  bool EncryptWallet(const SecureString& strWalletPassphrase);

  void GetKeyBirthTimes(std::map<ecdsa::CKeyID, int64_t>& mapKeyBirth) const;
  unsigned int ComputeTimeSmart(const CWalletTx& wtx) const;

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
  CAmount GetZerocoinBalance(bool fMatureOnly) const;
  CAmount GetUnconfirmedZerocoinBalance() const;
  CAmount GetImmatureZerocoinBalance() const;
  CAmount GetLockedCoins() const;
  CAmount GetUnlockedCoins() const;
  std::map<libzerocoin::CoinDenomination, CAmount> GetMyZerocoinDistribution() const;
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
  bool CreateCoinStake(const CKeyStore& keystore, unsigned int nBits, int64_t nSearchInterval,
                       CMutableTransaction& txNew, unsigned int& nTxNewTime);
  bool MultiSend();
  void AutoCombineDust();

  static CFeeRate minTxFee;
  static CAmount GetMinimumFee(unsigned int nTxBytes, unsigned int nConfirmTarget, const CTxMemPool& pool);

  bool NewKeyPool();
  bool TopUpKeyPool(unsigned int kpSize = 0);
  void ReserveKeyFromKeyPool(int64_t& nIndex, CKeyPool& keypool);
  void KeepKey(int64_t nIndex);
  void ReturnKey(int64_t nIndex);
  bool GetKeyFromPool(ecdsa::CPubKey& key);
  int64_t GetOldestKeyPoolTime();
  void GetAllReserveKeys(std::set<ecdsa::CKeyID>& setAddress) const;

  std::set<std::set<CTxDestination> > GetAddressGroupings();
  std::map<CTxDestination, CAmount> GetAddressBalances();

  std::set<CTxDestination> GetAccountAddresses(const std::string& strAccount) const;

  isminetype IsMine(const CTxIn& txin) const;
  CAmount GetDebit(const CTxIn& txin, const isminefilter& filter) const;
  isminetype IsMine(const CTxOut& txout) const { return ::IsMine(*this, txout.scriptPubKey); }
  bool IsMyZerocoinSpend(const CBigNum& bnSerial) const;
  bool IsMyMint(const CBigNum& bnValue) const;
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

  unsigned int GetKeyPoolSize() {
    AssertLockHeld(cs_wallet);  // setKeyPool
    return setKeyPool.size();
  }

  bool SetDefaultKey(const ecdsa::CPubKey& vchPubKey);

  CWallet* CreateWalletFromFile(const std::string& walletFile);

  /* Set the HD chain model (chain child index counters) */
  bool SetHDChain(const CHDChain& chain, bool memonly);
  const CHDChain& GetHDChain() { return hdChain; }

  /* Returns true if HD is enabled */
  bool IsHDEnabled();

  /* Generates a new HD master key (will not be activated) */
  ecdsa::CPubKey GenerateNewHDMasterKey();
  uint256 GetMasterKeySeed();  // returns HDMasterKey as uint256 for ZKP
  bool SetHDMasterKeyFromSeed(const uint256 seed);

  /* Set the current HD master key (will reset the chain child index counters)
   */
  bool SetHDMasterKey(const ecdsa::CPubKey& key);

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
  boost::signals2::signal<void(CWallet* wallet, const CTxDestination& address, const std::string& label, bool isMine,
                               const std::string& purpose, ChangeType status)>
      NotifyAddressBookChanged;

  /**
   * Wallet transaction added, removed or updated.
   * @note called with lock cs_wallet held.
   */
  boost::signals2::signal<void(CWallet* wallet, const uint256& hashTx, ChangeType status)> NotifyTransactionChanged;

  /** Show progress e.g. for rescan */
  boost::signals2::signal<void(const std::string& title, int nProgress)> ShowProgress;

  /** Watch-only address added */
  boost::signals2::signal<void(bool fHaveWatchOnly)> NotifyWatchonlyChanged;

  /** MultiSig address added */
  boost::signals2::signal<void(bool fHaveMultiSig)> NotifyMultiSigChanged;

  /** ZKP reset */
  boost::signals2::signal<void()> NotifyZkpReset;

  /** notify wallet file backed up */
  boost::signals2::signal<void(const bool& fSuccess, const std::string& filename)> NotifyWalletBacked;
};

#include "output.h"
#include "wallettx.h"
