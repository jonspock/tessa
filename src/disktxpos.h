// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The ClubChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "chain.h"
#include "serialize.h"

struct CDiskTxPos : public CDiskBlockPos {
  unsigned int nTxOffset;  // after header

  ADD_SERIALIZE_METHODS;

  template <typename Stream, typename Operation>
  inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
    READWRITE(*(CDiskBlockPos*)this);
    READWRITE(VARINT(nTxOffset));
  }

  CDiskTxPos(const CDiskBlockPos& blockIn, unsigned int nTxOffsetIn)
      : CDiskBlockPos(blockIn.nFile, blockIn.nPos), nTxOffset(nTxOffsetIn) {}

  CDiskTxPos() { SetNull(); }

  void SetNull() {
    CDiskBlockPos::SetNull();
    nTxOffset = 0;
  }
};
