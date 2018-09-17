#pragma once

#include "uint256.h"
#include <unordered_map>

class CBlockIndex;

struct BlockHasher {
  size_t operator()(const uint256& hash) const { return hash.GetLow64(); }
};

typedef std::unordered_map<uint256, CBlockIndex*, BlockHasher> BlockMap;

extern BlockMap mapBlockIndex;
