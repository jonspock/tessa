// Copyright (c) 2018 The PIVX developers
// Copyright (c) 2018 The ClubChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "accumulatorcheckpoints.h"

namespace AccumulatorCheckpoints {

// Only used here
std::map<int, Checkpoint> mapAccCheckpoints;

bool LoadCheckpoints(const std::string& strNetwork) {
  // Just start with Checkpoints all 0s initially
  Checkpoint checkpoint;
  CBigNum bn(0);
  int StartHeight = 0;
  for (auto denom : libzerocoin::zerocoinDenomList) checkpoint.insert(std::make_pair(denom, bn));
  mapAccCheckpoints.insert(make_pair(StartHeight, checkpoint));
  return true;
}

// For now just get initial 0 checkpoints
//
Checkpoint GetClosestCheckpoint(const int& nHeight, int& nHeightCheckpoint) {
  nHeightCheckpoint = -1;
  for (auto it : mapAccCheckpoints) {
    // only checkpoints that are less than the height requested (unless height is less than the first checkpoint)
    if (it.first < nHeight) {
      if (nHeightCheckpoint == -1) nHeightCheckpoint = it.first;
      if (nHeight - it.first < nHeightCheckpoint) nHeightCheckpoint = it.first;
    }
  }

  if (nHeightCheckpoint != -1) return mapAccCheckpoints.at(nHeightCheckpoint);

  return Checkpoint();
}
}  // namespace AccumulatorCheckpoints
