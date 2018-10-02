// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_AMOUNT_H
#define BITCOIN_AMOUNT_H

#include "serialize.h"

#include <cstdlib>
#include <string>

using CAmount = int64_t;

static const CAmount COIN = 100000000;
static const CAmount CENT = 1000000;

/** Type-safe wrapper class to for fee rates
 * (how much to pay based on transaction size)
 */
class CFeeRate {
 private:
  CAmount fee;  // unit is satoshis-per-1,000-bytes
 public:
  CFeeRate() : fee(1000) {}
  explicit CFeeRate(const CAmount& _fee) : fee(_fee) {}
  CFeeRate(const CAmount& nFeePaid, size_t nSize);
  CFeeRate(const CFeeRate& other) { fee = other.fee; }

  CAmount GetFee() const; 

  friend bool operator<(const CFeeRate& a, const CFeeRate& b) { return a.fee < b.fee; }
  friend bool operator>(const CFeeRate& a, const CFeeRate& b) { return a.fee > b.fee; }
  friend bool operator==(const CFeeRate& a, const CFeeRate& b) { return a.fee == b.fee; }
  friend bool operator<=(const CFeeRate& a, const CFeeRate& b) { return a.fee <= b.fee; }
  friend bool operator>=(const CFeeRate& a, const CFeeRate& b) { return a.fee >= b.fee; }
  std::string ToString() const;

  ADD_SERIALIZE_METHODS

  template <typename Stream, typename Operation> inline void SerializationOp(Stream& s, Operation ser_action) {
    READWRITE(fee);
  }
};

#endif  //  BITCOIN_AMOUNT_H
