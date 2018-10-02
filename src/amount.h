// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "serialize.h"

#include <cstdlib>
#include <string>
#include "coin_constants.h"

using CAmount = int64_t;

static const CAmount COIN = COIN_AMOUNT;
static const CAmount COINCENT = COINCENT_AMOUNT;

/** Type-safe wrapper class to for fee rates */
class CFeeRate {
 private:
  CAmount fee = COINCENT;

 public:
  CFeeRate() = default;
  explicit CFeeRate(const CAmount& _fee) : fee(_fee) {}
  CFeeRate(const CFeeRate& other) { fee = other.fee; }

  CAmount GetFee() const { return fee; }

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
