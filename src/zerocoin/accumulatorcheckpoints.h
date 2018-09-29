// Copyright (c) 2018 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "bignum.h"
#include "libzerocoin/Denominations.h"

namespace AccumulatorCheckpoints {
typedef std::map<libzerocoin::CoinDenomination, CBigNum> Checkpoint;

bool LoadCheckpoints(const std::string& strNetwork);
Checkpoint GetClosestCheckpoint(const int& nHeight, int& nHeightCheckpoint);
}  // namespace AccumulatorCheckpoints
