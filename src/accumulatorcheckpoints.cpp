// Copyright (c) 2018 The PIVX developers
// Copyright (c) 2018 The ClubChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "accumulatorcheckpoints.h"
#include <iostream>
#include "zerochain.h"

namespace AccumulatorCheckpoints {
std::map<int, Checkpoint> mapCheckpoints;

UniValue read_json(const std::string& jsondata) {
  UniValue v;

  if (!v.read(jsondata) || !v.isArray()) { return UniValue(UniValue::VARR); }
  return v.get_array();
}

bool LoadCheckpoints(const std::string& strNetwork) {
    // Just start with Checkpoints all 0s initially
    Checkpoint checkpoint;
    CBigNum bn(0);
#warning "Check checkpoints"
    int StartHeight=100;
    for (auto denom : libzerocoin::zerocoinDenomList) checkpoint.insert(std::make_pair(denom, bn));
    mapCheckpoints.insert(make_pair(StartHeight, checkpoint));
    return true;
}

Checkpoint GetClosestCheckpoint(const int& nHeight, int& nHeightCheckpoint) {
  nHeightCheckpoint = -1;
  for (auto it : mapCheckpoints) {
    // only checkpoints that are less than the height requested (unless height is less than the first checkpoint)
    if (it.first < nHeight) {
      if (nHeightCheckpoint == -1) nHeightCheckpoint = it.first;
      if (nHeight - it.first < nHeightCheckpoint) nHeightCheckpoint = it.first;
    }
  }

  if (nHeightCheckpoint != -1) return mapCheckpoints.at(nHeightCheckpoint);

  return Checkpoint();
}
}  // namespace AccumulatorCheckpoints
