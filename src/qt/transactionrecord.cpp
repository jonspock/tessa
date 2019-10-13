// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "transactionrecord.h"
#include "blockmap.h"
#include "chain.h"
#include "timedata.h"
#include "wallet/wallet.h"
#include "wallet/wallettx.h"
#include "main.h"

#include <stdint.h>

/* Return positive answer if transaction should be shown in list.
 */
bool TransactionRecord::showTransaction(const CWalletTx& wtx) {
  if (wtx.IsCoinBase()) {
    // Ensures we show generated coins / mined transactions at depth 1
    if (!wtx.IsInMainChain()) { return false; }
  }
  return true;
}

/*
 * Decompose CWallet transaction to model transaction records.
 */
QList<TransactionRecord> TransactionRecord::decomposeTransaction(const CWallet* wallet, const CWalletTx& wtx) {
  QList<TransactionRecord> parts;
  int64_t nTime = wtx.GetComputedTxTime();
  CAmount nCredit = wtx.GetCredit(ISMINE_ALL);
  CAmount nDebit = wtx.GetDebit(ISMINE_ALL);
  CAmount nNet = nCredit - nDebit;
  uint256 hash = wtx.GetHash();
  std::map<std::string, std::string> mapValue = wtx.mapValue;
  bool fZSpendFromMe = false;

  if (wtx.IsCoinStake()) {
    TransactionRecord sub(hash, nTime);
    CTxDestination address;
    if (!ExtractDestination(wtx.vout[1].scriptPubKey, address)) return parts;


    if (isminetype mine = wallet->IsMine(wtx.vout[1])) {
      // Tessa stake reward
      sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
      sub.type = TransactionRecord::StakeMint;
      sub.address = EncodeDestination(address);
      sub.credit = nNet;
    }

    

    parts.append(sub);
  } else if (nNet > 0 || wtx.IsCoinBase()) {
    //
    // Credit
    //
    for (const CTxOut& txout : wtx.vout) {
      isminetype mine = wallet->IsMine(txout);
      if (mine) {
        TransactionRecord sub(hash, nTime);
        CTxDestination address;
        sub.idx = parts.size();  // sequence number
        sub.credit = txout.nValue;
        sub.involvesWatchAddress = mine & ISMINE_WATCH_ONLY;
        if (ExtractDestination(txout.scriptPubKey, address) && IsMine(*wallet, address)) {
          // Received by Tessa Address
          sub.type = TransactionRecord::RecvWithAddress;
          sub.address = EncodeDestination(address);
        } else {
          // Received by IP connection (deprecated features), or a multisignature or other non-simple transaction
          sub.type = TransactionRecord::RecvFromOther;
          sub.address = mapValue["from"];
        }
        if (wtx.IsCoinBase()) {
          // Generated
          sub.type = TransactionRecord::Generated;
        }

        parts.append(sub);
      }
    }
  } else {
    int nFromMe = 0;
    bool involvesWatchAddress = false;
    isminetype fAllFromMe = ISMINE_SPENDABLE;
    for (const CTxIn& txin : wtx.vin) {
      if (wallet->IsMine(txin)) { nFromMe++; }
      isminetype mine = wallet->IsMine(txin);
      if (mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
      if (fAllFromMe > mine) fAllFromMe = mine;
    }

    isminetype fAllToMe = ISMINE_SPENDABLE;
    int nToMe = 0;
    for (const CTxOut& txout : wtx.vout) {
      if (wallet->IsMine(txout)) { nToMe++; }
      isminetype mine = wallet->IsMine(txout);
      if (mine & ISMINE_WATCH_ONLY) involvesWatchAddress = true;
      if (fAllToMe > mine) fAllToMe = mine;
    }

    if (fAllFromMe && fAllToMe) {
      // Payment to self
      // TODO: this section still not accurate but covers most cases,
      // might need some additional work however

      TransactionRecord sub(hash, nTime);
      // Payment to self by default
      sub.type = TransactionRecord::SendToSelf;
      sub.address = "";

      for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++) { sub.idx = parts.size(); }

      CAmount nChange = wtx.GetChange();

      sub.debit = -(nDebit - nChange);
      sub.credit = nCredit - nChange;
      parts.append(sub);
      parts.last().involvesWatchAddress =
          involvesWatchAddress;  // maybe pass to TransactionRecord as constructor argument
    } else if (fAllFromMe) {
      //
      // Debit
      //
      CAmount nTxFee = nDebit - wtx.GetValueOut();

      for (unsigned int nOut = 0; nOut < wtx.vout.size(); nOut++) {
        const CTxOut& txout = wtx.vout[nOut];
        TransactionRecord sub(hash, nTime);
        sub.idx = parts.size();
        sub.involvesWatchAddress = involvesWatchAddress;

        if (wallet->IsMine(txout)) {
          // Ignore parts sent to self, as this is usually the change
          // from a transaction sent back to our own address.
          continue;
        }

        CTxDestination address;
        if (ExtractDestination(txout.scriptPubKey, address)) {
          // This is most likely only going to happen when resyncing deterministic wallet without the knowledge of the
          // private keys that the change was sent to. Do not display a "sent to" here.
          // Sent to Tessa Address
          sub.type = TransactionRecord::SendToAddress;
          sub.address = EncodeDestination(address);
        } else {
          // Sent to IP, or other non-address transaction like OP_EVAL
          sub.type = TransactionRecord::SendToOther;
          sub.address = mapValue["to"];
        }

        CAmount nValue = txout.nValue;
        /* Add fee to first output */
        if (nTxFee > 0) {
          nValue += nTxFee;
          nTxFee = 0;
        }
        sub.debit = -nValue;

        parts.append(sub);
      }
    } else {
      //
      // Mixed debit transaction, can't break down payees
      //
      parts.append(TransactionRecord(hash, nTime, TransactionRecord::Other, "", nNet, 0));
      parts.last().involvesWatchAddress = involvesWatchAddress;
    }
  }

  return parts;
}

void TransactionRecord::updateStatus(const CWalletTx& wtx) {
  AssertLockHeld(cs_main);
  // Determine transaction status

  // Find the block the tx is in
  CBlockIndex* pindex = nullptr;
  BlockMap::iterator mi = mapBlockIndex.find(wtx.hashBlock);
  if (mi != mapBlockIndex.end()) pindex = (*mi).second;

  // Sort order, unrecorded transactions sort to the top
  status.sortKey = strprintf("%010d-%01d-%010u-%03d", (pindex ? pindex->nHeight : std::numeric_limits<int>::max()),
                             (wtx.IsCoinBase() ? 1 : 0), wtx.nTimeReceived, idx);
  // status.countsForBalance = wtx.IsTrusted() && !(wtx.GetBlocksToMaturity() > 0);
  status.depth = wtx.GetDepthInMainChain();

  // Determine the depth of the block
  int nBlocksToMaturity = wtx.GetBlocksToMaturity();

  status.countsForBalance = wtx.IsTrusted() && !(nBlocksToMaturity > 0);
  status.cur_num_blocks = chainActive.Height();

  if (!IsFinalTx(wtx, chainActive.Height() + 1)) {
    if (wtx.nLockTime < LOCKTIME_THRESHOLD) {
      status.status = TransactionStatus::OpenUntilBlock;
      status.open_for = wtx.nLockTime - chainActive.Height();
    } else {
      status.status = TransactionStatus::OpenUntilDate;
      status.open_for = wtx.nLockTime;
    }
  }
  // For generated transactions, determine maturity
  else if (type == TransactionRecord::Generated || type == TransactionRecord::StakeMint) {
    if (nBlocksToMaturity > 0) {
      status.status = TransactionStatus::Immature;
      status.matures_in = nBlocksToMaturity;

      if (pindex && chainActive.Contains(pindex)) {
        // Check if the block was requested by anyone
        if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0)
          status.status = TransactionStatus::MaturesWarning;
      } else {
        status.status = TransactionStatus::NotAccepted;
      }
    } else {
      status.status = TransactionStatus::Confirmed;
      status.matures_in = 0;
    }
  } else {
    if (status.depth < 0) {
      status.status = TransactionStatus::Conflicted;
    } else if (GetAdjustedTime() - wtx.nTimeReceived > 2 * 60 && wtx.GetRequestCount() == 0) {
      status.status = TransactionStatus::Offline;
    } else if (status.depth == 0) {
      status.status = TransactionStatus::Unconfirmed;
    } else if (status.depth < RecommendedNumConfirmations) {
      status.status = TransactionStatus::Confirming;
    } else {
      status.status = TransactionStatus::Confirmed;
    }
  }
}

bool TransactionRecord::statusUpdateNeeded() {
  AssertLockHeld(cs_main);
  return status.cur_num_blocks != chainActive.Height();
}

QString TransactionRecord::getTxID() const { return QString::fromStdString(hash.ToString()); }

int TransactionRecord::getOutputIndex() const { return idx; }
