// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The ClubChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once
#include "utilstrencodings.h"

typedef std::map<std::string, std::string> mapValue_t;

struct COutputEntry {
  CTxDestination destination;
  CAmount amount;
  int vout;
};

/** A transaction with a merkle branch linking it to the block chain. */
class CMerkleTx : public CTransaction {
 private:
  int GetDepthInMainChainINTERNAL(const CBlockIndex*& pindexRet) const;

 public:
  uint256 hashBlock;
  std::vector<uint256> vMerkleBranch;
  int nIndex;

  // memory only
  mutable bool fMerkleVerified;

  CMerkleTx() { Init(); }

  CMerkleTx(const CTransaction& txIn) : CTransaction(txIn) { Init(); }

  void Init() {
    hashBlock = 0;
    nIndex = -1;
    fMerkleVerified = false;
  }

  ADD_SERIALIZE_METHODS;

  template <typename Stream, typename Operation>
  inline void SerializationOp(Stream& s, Operation ser_action) {
    READWRITE(*(CTransaction*)this);
    //nVersion = this->nTransactionVersion;
    READWRITE(hashBlock);
    READWRITE(vMerkleBranch);
    READWRITE(nIndex);
  }

  int SetMerkleBranch(const CBlock& block);

  /**
   * Return depth of transaction in blockchain:
   * -1  : not in blockchain, and not in memory pool (conflicted transaction)
   *  0  : in memory pool, waiting to be included in a block
   * >=1 : this many blocks deep in the main chain
   */
  int GetDepthInMainChain(const CBlockIndex*& pindexRet, bool enableIX = true) const;
  int GetDepthInMainChain(bool enableIX = true) const {
    const CBlockIndex* pindexRet;
    return GetDepthInMainChain(pindexRet, enableIX);
  }
  bool IsInMainChain() const {
    const CBlockIndex* pindexRet;
    return GetDepthInMainChainINTERNAL(pindexRet) > 0;
  }
  int GetBlocksToMaturity() const;
  bool AcceptToMemoryPool(bool fLimitFree = true, bool fRejectInsaneFee = true, bool ignoreFees = false);
  int GetTransactionLockSignatures() const;
  bool IsTransactionLockTimedOut() const;
};

/**
 * A transaction with a bunch of additional info that only the owner cares about.
 * It includes any unrecorded transactions needed to link it back to the block chain.
 */
class CWalletTx : public CMerkleTx {
 private:
  const CWallet* pwallet;

 public:
  mapValue_t mapValue;
  std::vector<std::pair<std::string, std::string> > vOrderForm;
  unsigned int fTimeReceivedIsTxTime;
  unsigned int nTimeReceived;  //! time received by this node
  unsigned int nTimeSmart;
  char fFromMe;
  std::string strFromAccount;
  int64_t nOrderPos;  //! position in ordered transaction list

  // memory only
  mutable bool fDebitCached;
  mutable bool fCreditCached;
  mutable bool fImmatureCreditCached;
  mutable bool fAvailableCreditCached;
  mutable bool fDenomUnconfCreditCached;
  mutable bool fDenomConfCreditCached;
  mutable bool fWatchDebitCached;
  mutable bool fWatchCreditCached;
  mutable bool fImmatureWatchCreditCached;
  mutable bool fAvailableWatchCreditCached;
  mutable bool fChangeCached;
  mutable CAmount nDebitCached;
  mutable CAmount nCreditCached;
  mutable CAmount nImmatureCreditCached;
  mutable CAmount nAvailableCreditCached;
  mutable CAmount nDenomUnconfCreditCached;
  mutable CAmount nDenomConfCreditCached;
  mutable CAmount nWatchDebitCached;
  mutable CAmount nWatchCreditCached;
  mutable CAmount nImmatureWatchCreditCached;
  mutable CAmount nAvailableWatchCreditCached;
  mutable CAmount nChangeCached;

  CWalletTx() { Init(nullptr); }

  CWalletTx(const CWallet* pwalletIn) { Init(pwalletIn); }

  CWalletTx(const CWallet* pwalletIn, const CMerkleTx& txIn) : CMerkleTx(txIn) { Init(pwalletIn); }

  CWalletTx(const CWallet* pwalletIn, const CTransaction& txIn) : CMerkleTx(txIn) { Init(pwalletIn); }

  void Init(const CWallet* pwalletIn) {
    pwallet = pwalletIn;
    mapValue.clear();
    vOrderForm.clear();
    fTimeReceivedIsTxTime = false;
    nTimeReceived = 0;
    nTimeSmart = 0;
    fFromMe = false;
    strFromAccount.clear();
    fDebitCached = false;
    fCreditCached = false;
    fImmatureCreditCached = false;
    fAvailableCreditCached = false;
    fDenomUnconfCreditCached = false;
    fDenomConfCreditCached = false;
    fWatchDebitCached = false;
    fWatchCreditCached = false;
    fImmatureWatchCreditCached = false;
    fAvailableWatchCreditCached = false;
    fChangeCached = false;
    nDebitCached = 0;
    nCreditCached = 0;
    nImmatureCreditCached = 0;
    nAvailableCreditCached = 0;
    nDenomUnconfCreditCached = 0;
    nDenomConfCreditCached = 0;
    nWatchDebitCached = 0;
    nWatchCreditCached = 0;
    nAvailableWatchCreditCached = 0;
    nImmatureWatchCreditCached = 0;
    nChangeCached = 0;
    nOrderPos = -1;
  }

  ADD_SERIALIZE_METHODS;

  template <typename Stream, typename Operation>
  inline void SerializationOp(Stream& s, Operation ser_action) {
    if (ser_action.ForRead()) Init(nullptr);
    char fSpent = false;

    if (!ser_action.ForRead()) {
      mapValue["fromaccount"] = strFromAccount;

      WriteOrderPos(nOrderPos, mapValue);

      if (nTimeSmart) mapValue["timesmart"] = strprintf("%u", nTimeSmart);
    }

    READWRITE(*(CMerkleTx*)this);
    std::vector<CMerkleTx> vUnused;  //! Used to be vtxPrev
    READWRITE(vUnused);
    READWRITE(mapValue);
    READWRITE(vOrderForm);
    READWRITE(fTimeReceivedIsTxTime);
    READWRITE(nTimeReceived);
    READWRITE(fFromMe);
    READWRITE(fSpent);

    if (ser_action.ForRead()) {
      strFromAccount = mapValue["fromaccount"];

      ReadOrderPos(nOrderPos, mapValue);

      nTimeSmart = mapValue.count("timesmart") ? (unsigned int)std::atoi(mapValue["timesmart"].c_str()) : 0;
    }

    mapValue.erase("fromaccount");
    mapValue.erase("version");
    mapValue.erase("spent");
    mapValue.erase("n");
    mapValue.erase("timesmart");
  }

  //! make sure balances are recalculated
  void MarkDirty() {
    fCreditCached = false;
    fAvailableCreditCached = false;
    fDenomUnconfCreditCached = false;
    fDenomConfCreditCached = false;
    fWatchDebitCached = false;
    fWatchCreditCached = false;
    fAvailableWatchCreditCached = false;
    fImmatureWatchCreditCached = false;
    fDebitCached = false;
    fChangeCached = false;
  }

  void BindWallet(CWallet* pwalletIn) {
    pwallet = pwalletIn;
    MarkDirty();
  }

  //! filter decides which addresses will count towards the debit
  CAmount GetDebit(const isminefilter& filter) const;
  CAmount GetCredit(const isminefilter& filter) const;
  CAmount GetImmatureCredit(bool fUseCache = true) const;
  CAmount GetAvailableCredit(bool fUseCache = true) const;
  // Return sum of unlocked coins
  CAmount GetUnlockedCredit() const;
  // Return sum of unlocked coins
  CAmount GetLockedCredit() const;
  CAmount GetImmatureWatchOnlyCredit(const bool& fUseCache = true) const;
  CAmount GetAvailableWatchOnlyCredit(const bool& fUseCache = true) const;
  CAmount GetLockedWatchOnlyCredit() const;

  CAmount GetChange() const {
    if (fChangeCached) return nChangeCached;
    nChangeCached = pwallet->GetChange(*this);
    fChangeCached = true;
    return nChangeCached;
  }

  void GetAmounts(std::list<COutputEntry>& listReceived, std::list<COutputEntry>& listSent, CAmount& nFee,
                  std::string& strSentAccount, const isminefilter& filter) const;

  void GetAccountAmounts(const std::string& strAccount, CAmount& nReceived, CAmount& nSent, CAmount& nFee,
                         const isminefilter& filter) const;

  bool IsFromMe(const isminefilter& filter) const { return (GetDebit(filter) > 0); }

  bool InMempool() const;

  bool IsTrusted() const {
    // Quick answer in most cases
    if (!IsFinalTx(*this)) return false;
    int nDepth = GetDepthInMainChain();
    if (nDepth >= 1) return true;
    if (nDepth < 0) return false;
    if (!bSpendZeroConfChange || !IsFromMe(ISMINE_ALL))  // using wtx's cached debit
      return false;

    // Trusted if all inputs are from us and are in the mempool:
    for (const CTxIn& txin : vin) {
      // Transactions not sent by us: not trusted
      const CWalletTx* parent = pwallet->GetWalletTx(txin.prevout.hash);
      if (parent == nullptr) return false;
      const CTxOut& parentOut = parent->vout[txin.prevout.n];
      if (pwallet->IsMine(parentOut) != ISMINE_SPENDABLE) return false;
    }
    return true;
  }

  bool WriteToDisk();

  int64_t GetTxTime() const;
  int64_t GetComputedTxTime() const;
  int GetRequestCount() const;
  void RelayWalletTransaction(std::string strCommand = "tx");

  std::set<uint256> GetConflicts() const;
};
