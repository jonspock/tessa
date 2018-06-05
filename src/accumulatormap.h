// Copyright (c) 2017-2018 The PIVX developers
// Copyright (c) 2018 The ClubChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "accumulatorcheckpoints.h"
#include "libzerocoin/Accumulator.h"
#include "libzerocoin/PublicCoin.h"

// A map with an accumulator for each denomination
class AccumulatorMap {
 private:
  libzerocoin::ZerocoinParams* params;
  std::map<libzerocoin::CoinDenomination, std::unique_ptr<libzerocoin::Accumulator> > mapAccumValues;

 public:
  explicit AccumulatorMap(libzerocoin::ZerocoinParams* params);
  bool Load(uint256 nCheckpoint);
  void Load(const AccumulatorCheckpoints::Checkpoint& checkpoint);
  bool Accumulate(const libzerocoin::PublicCoin& pubCoin, bool fSkipValidation = false);
  CBigNum GetValue(libzerocoin::CoinDenomination denom);
  uint256 GetCheckpoint();
  void Reset();
  void Reset(libzerocoin::ZerocoinParams* params2);
};
