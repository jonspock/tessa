// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2013 The Bitcoin developers
// Copyright (c) 2016-2017 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_UNDO_H
#define BITCOIN_UNDO_H

#include "compressor.h"
#include "primitives/transaction.h"
#include "serialize.h"

/** Undo information for a CTxIn
 *
 *  Contains the prevout's CTxOut being spent, and if this was the
 *  last output of the affected transaction, its metadata as well
 *  (coinbase or not, height, transaction version)
 */
class CTxInUndo {
 public:
  CTxOut txout;    // the txout data before being spent
  bool fCoinBase;  // if the outpoint was the last unspent: whether it belonged to a coinbase
  bool fCoinStake;
  uint32_t nHeight;         // if the outpoint was the last unspent: its height
  int nTransactionVersion;  // if the outpoint was the last unspent: its version

  CTxInUndo() : txout(), fCoinBase(false), fCoinStake(false), nHeight(0), nTransactionVersion(0) {}
  CTxInUndo(const CTxOut& txoutIn, bool fCoinBaseIn = false, bool fCoinStakeIn = false, uint32_t nHeightIn = 0,
            int nVersionIn = 0)
      : txout(txoutIn),
        fCoinBase(fCoinBaseIn),
        fCoinStake(fCoinStakeIn),
        nHeight(nHeightIn),
        nTransactionVersion(nVersionIn) {}

  uint32_t GetSerializeSize() const {
    return ::GetSerializeSize(VARINT(nHeight * 4 + (fCoinBase ? 2 : 0) + (fCoinStake ? 1 : 0))) +
           (nHeight > 0 ? ::GetSerializeSize(VARINT(nTransactionVersion)) : 0) +
           ::GetSerializeSize(CTxOutCompressor(REF(txout)));
  }

  template <typename Stream> void Serialize(Stream& s) const {
    ::Serialize(s, VARINT(nHeight * 4 + (fCoinBase ? 2 : 0) + (fCoinStake ? 1 : 0)));
    if (nHeight > 0) ::Serialize(s, VARINT(nTransactionVersion));
    ::Serialize(s, CTxOutCompressor(REF(txout)));
  }

  template <typename Stream> void Unserialize(Stream& s) {
    uint32_t nCode = 0;
    ::Unserialize(s, VARINT(nCode));
    nHeight = nCode >> 2;
    fCoinBase = nCode & 2;
    fCoinStake = nCode & 1;
    if (nHeight > 0) ::Unserialize(s, VARINT(nTransactionVersion));
    ::Unserialize(s, REF(CTxOutCompressor(REF(txout))));
  }
};

/** Undo information for a CTransaction */
class CTxUndo {
 public:
  // undo information for all txins
  std::vector<CTxInUndo> vprevout;

  ADD_SERIALIZE_METHODS

  template <typename Stream, typename Operation> inline void SerializationOp(Stream& s, Operation ser_action) {
    READWRITE(vprevout);
  }
};

#endif  // BITCOIN_UNDO_H
