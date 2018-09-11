// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockmap.h"
#include "chain.h"
#include "init.h"
#include "net.h"  // need all of that?
#include "tessa_externs.h"
#include "uint256.h"
#include "wallet.h"
#include <cassert>

using namespace std;

/**
 * Settings
 */

int64_t CWalletTx::GetTxTime() const {
  int64_t n = nTimeSmart;
  return n ? n : nTimeReceived;
}

int64_t CWalletTx::GetComputedTxTime() const {
  if (IsZerocoinSpend() || IsZerocoinMint()) {
    if (IsInMainChain())
      return mapBlockIndex.at(hashBlock)->GetBlockTime();
    else
      return nTimeReceived;
  }
  return GetTxTime();
}

int CWalletTx::GetRequestCount() const {
  // Returns -1 if it wasn't being tracked
  int nRequests = -1;
  {
    LOCK(pwallet->cs_wallet);
    if (IsCoinBase()) {
      // Generated block
      if (!hashBlock.IsNull()) {
        auto mi = pwallet->mapRequestCount.find(hashBlock);
        if (mi != pwallet->mapRequestCount.end()) nRequests = (*mi).second;
      }
    } else {
      // Did anyone request this transaction?
      auto mi = pwallet->mapRequestCount.find(GetHash());
      if (mi != pwallet->mapRequestCount.end()) {
        nRequests = (*mi).second;

        // How about the block it's in?
        if (nRequests == 0 && !hashBlock.IsNull()) {
          auto mi = pwallet->mapRequestCount.find(hashBlock);
          if (mi != pwallet->mapRequestCount.end())
            nRequests = (*mi).second;
          else
            nRequests = 1;  // If it's in someone else's block it must have got out
        }
      }
    }
  }
  return nRequests;
}

//! filter decides which addresses will count towards the debit
CAmount CWalletTx::GetDebit(const isminefilter& filter) const {
  if (vin.empty()) return 0;

  CAmount debit = 0;
  if (filter & ISMINE_SPENDABLE) {
    if (fDebitCached)
      debit += nDebitCached;
    else {
      nDebitCached = pwallet->GetDebit(*this, ISMINE_SPENDABLE);
      fDebitCached = true;
      debit += nDebitCached;
    }
  }
  if (filter & ISMINE_WATCH_ONLY) {
    if (fWatchDebitCached)
      debit += nWatchDebitCached;
    else {
      nWatchDebitCached = pwallet->GetDebit(*this, ISMINE_WATCH_ONLY);
      fWatchDebitCached = true;
      debit += nWatchDebitCached;
    }
  }
  return debit;
}

CAmount CWalletTx::GetCredit(const isminefilter& filter) const {
  // Must wait until coinbase is safely deep enough in the chain before valuing it
  if (IsCoinBase() && GetBlocksToMaturity() > 0) return 0;

  CAmount credit = 0;
  if (filter & ISMINE_SPENDABLE) {
    // GetBalance can assume transactions in mapWallet won't change
    if (fCreditCached)
      credit += nCreditCached;
    else {
      nCreditCached = pwallet->GetCredit(*this, ISMINE_SPENDABLE);
      fCreditCached = true;
      credit += nCreditCached;
    }
  }
  if (filter & ISMINE_WATCH_ONLY) {
    if (fWatchCreditCached)
      credit += nWatchCreditCached;
    else {
      nWatchCreditCached = pwallet->GetCredit(*this, ISMINE_WATCH_ONLY);
      fWatchCreditCached = true;
      credit += nWatchCreditCached;
    }
  }
  return credit;
}

CAmount CWalletTx::GetImmatureCredit(bool fUseCache) const {
  if ((IsCoinBase() || IsCoinStake()) && GetBlocksToMaturity() > 0 && IsInMainChain()) {
    if (fUseCache && fImmatureCreditCached) return nImmatureCreditCached;
    nImmatureCreditCached = pwallet->GetCredit(*this, ISMINE_SPENDABLE);
    fImmatureCreditCached = true;
    return nImmatureCreditCached;
  }

  return 0;
}

CAmount CWalletTx::GetAvailableCredit(bool fUseCache) const {
  if (pwallet == nullptr) return 0;

  // Must wait until coinbase is safely deep enough in the chain before valuing it
  if (IsCoinBase() && GetBlocksToMaturity() > 0) return 0;

  if (fUseCache && fAvailableCreditCached) return nAvailableCreditCached;

  CAmount nCredit = 0;
  uint256 hashTx = GetHash();
  for (uint32_t i = 0; i < vout.size(); i++) {
    if (!pwallet->IsSpent(hashTx, i)) {
      const CTxOut& txout = vout[i];
      nCredit += pwallet->GetCredit(txout, ISMINE_SPENDABLE);
      if (!MoneyRange(nCredit)) throw std::runtime_error("CWalletTx::GetAvailableCredit() : value out of range");
    }
  }

  nAvailableCreditCached = nCredit;
  fAvailableCreditCached = true;
  return nCredit;
}

// Return sum of unlocked coins
CAmount CWalletTx::GetUnlockedCredit() const {
  if (pwallet == nullptr) return 0;

  // Must wait until coinbase is safely deep enough in the chain before valuing it
  if (IsCoinBase() && GetBlocksToMaturity() > 0) return 0;

  CAmount nCredit = 0;
  uint256 hashTx = GetHash();
  for (uint32_t i = 0; i < vout.size(); i++) {
    const CTxOut& txout = vout[i];

    if (pwallet->IsSpent(hashTx, i) || pwallet->IsLockedCoin(hashTx, i)) continue;

    nCredit += pwallet->GetCredit(txout, ISMINE_SPENDABLE);
    if (!MoneyRange(nCredit)) throw std::runtime_error("CWalletTx::GetUnlockedCredit() : value out of range");
  }

  return nCredit;
}

// Return sum of unlocked coins
CAmount CWalletTx::GetLockedCredit() const {
  if (pwallet == nullptr) return 0;

  // Must wait until coinbase is safely deep enough in the chain before valuing it
  if (IsCoinBase() && GetBlocksToMaturity() > 0) return 0;

  CAmount nCredit = 0;
  uint256 hashTx = GetHash();
  for (uint32_t i = 0; i < vout.size(); i++) {
    const CTxOut& txout = vout[i];

    // Skip spent coins
    if (pwallet->IsSpent(hashTx, i)) continue;

    // Add locked coins
    if (pwallet->IsLockedCoin(hashTx, i)) { nCredit += pwallet->GetCredit(txout, ISMINE_SPENDABLE); }

    if (!MoneyRange(nCredit)) throw std::runtime_error("CWalletTx::GetLockedCredit() : value out of range");
  }

  return nCredit;
}

CAmount CWalletTx::GetImmatureWatchOnlyCredit(const bool& fUseCache) const {
  if (IsCoinBase() && GetBlocksToMaturity() > 0 && IsInMainChain()) {
    if (fUseCache && fImmatureWatchCreditCached) return nImmatureWatchCreditCached;
    nImmatureWatchCreditCached = pwallet->GetCredit(*this, ISMINE_WATCH_ONLY);
    fImmatureWatchCreditCached = true;
    return nImmatureWatchCreditCached;
  }

  return 0;
}

CAmount CWalletTx::GetAvailableWatchOnlyCredit(const bool& fUseCache) const {
  if (pwallet == nullptr) return 0;

  // Must wait until coinbase is safely deep enough in the chain before valuing it
  if (IsCoinBase() && GetBlocksToMaturity() > 0) return 0;

  if (fUseCache && fAvailableWatchCreditCached) return nAvailableWatchCreditCached;

  CAmount nCredit = 0;
  for (uint32_t i = 0; i < vout.size(); i++) {
    if (!pwallet->IsSpent(GetHash(), i)) {
      const CTxOut& txout = vout[i];
      nCredit += pwallet->GetCredit(txout, ISMINE_WATCH_ONLY);
      if (!MoneyRange(nCredit)) throw std::runtime_error("CWalletTx::GetAvailableCredit() : value out of range");
    }
  }

  nAvailableWatchCreditCached = nCredit;
  fAvailableWatchCreditCached = true;
  return nCredit;
}

CAmount CWalletTx::GetLockedWatchOnlyCredit() const {
  if (pwallet == nullptr) return 0;

  // Must wait until coinbase is safely deep enough in the chain before valuing it
  if (IsCoinBase() && GetBlocksToMaturity() > 0) return 0;

  CAmount nCredit = 0;
  uint256 hashTx = GetHash();
  for (uint32_t i = 0; i < vout.size(); i++) {
    const CTxOut& txout = vout[i];

    // Skip spent coins
    if (pwallet->IsSpent(hashTx, i)) continue;

    // Add locked coins
    if (pwallet->IsLockedCoin(hashTx, i)) { nCredit += pwallet->GetCredit(txout, ISMINE_WATCH_ONLY); }

    if (!MoneyRange(nCredit)) throw std::runtime_error("CWalletTx::GetLockedCredit() : value out of range");
  }

  return nCredit;
}

void CWalletTx::GetAmounts(list<COutputEntry>& listReceived, list<COutputEntry>& listSent, CAmount& nFee,
                           string& strSentAccount, const isminefilter& filter) const {
  nFee = 0;
  listReceived.clear();
  listSent.clear();
  strSentAccount = strFromAccount;

  // Compute fee:
  CAmount nDebit = GetDebit(filter);
  if (nDebit > 0)  // debit>0 means we signed/sent this transaction
  {
    CAmount nValueOut = GetValueOut();
    nFee = nDebit - nValueOut;
  }

  // Sent/received.
  for (uint32_t i = 0; i < vout.size(); ++i) {
    const CTxOut& txout = vout[i];
    isminetype fIsMine = pwallet->IsMine(txout);
    // Only need to handle txouts if AT LEAST one of these is true:
    //   1) they debit from us (sent)
    //   2) the output is to us (received)
    if (nDebit > 0) {
      // Don't report 'change' txouts
      if (pwallet->IsChange(txout)) continue;
    } else if (!(fIsMine & filter) && !IsZerocoinSpend())
      continue;

    // In either case, we need to get the destination address
    CTxDestination address;
    if (txout.scriptPubKey.IsZerocoinMint()) {
      address = CNoDestination();
    } else if (!ExtractDestination(txout.scriptPubKey, address)) {
      if (!IsCoinStake() && !IsCoinBase()) {
        LogPrintf("CWalletTx::GetAmounts: Unknown transaction type found, txid %s\n", this->GetHash().ToString());
      }
      address = CNoDestination();
    }

    COutputEntry output = {address, txout.nValue, (int)i};

    // If we are debited by the transaction, add the output as a "sent" entry
    if (nDebit > 0) listSent.push_back(output);

    // If we are receiving the output, add it as a "received" entry
    if (fIsMine & filter) listReceived.push_back(output);
  }
}

void CWalletTx::GetAccountAmounts(const string& strAccount, CAmount& nReceived, CAmount& nSent, CAmount& nFee,
                                  const isminefilter& filter) const {
  nReceived = nSent = nFee = 0;

  CAmount allFee;
  string strSentAccount;
  list<COutputEntry> listReceived;
  list<COutputEntry> listSent;
  GetAmounts(listReceived, listSent, allFee, strSentAccount, filter);

  if (strAccount == strSentAccount) {
    for (const COutputEntry& s : listSent) nSent += s.amount;
    nFee = allFee;
  }
  {
    LOCK(pwallet->cs_wallet);
    for (const COutputEntry& r : listReceived) {
      if (pwallet->mapAddressBook.count(r.destination)) {
        auto mi = pwallet->mapAddressBook.find(r.destination);
        if (mi != pwallet->mapAddressBook.end() && (*mi).second.name == strAccount) nReceived += r.amount;
      } else if (strAccount.empty()) {
        nReceived += r.amount;
      }
    }
  }
}

bool CWalletTx::WriteToDisk() { return gWalletDB.WriteTx(GetHash(), *this); }

bool CWalletTx::InMempool() const {
  LOCK(mempool.cs);
  if (mempool.exists(GetHash())) { return true; }
  return false;
}

void CWalletTx::RelayWalletTransaction(std::string strCommand) {
  if (!IsCoinBase()) {
    if (GetDepthInMainChain() == 0) {
      uint256 hash = GetHash();
      LogPrintf("Relaying wtx %s\n", hash.ToString());
      RelayTransaction((CTransaction) * this);
    }
  }
}

set<uint256> CWalletTx::GetConflicts() const {
  set<uint256> result;
  if (pwallet != nullptr) {
    uint256 myHash = GetHash();
    result = pwallet->GetConflicts(myHash);
    result.erase(myHash);
  }
  return result;
}
