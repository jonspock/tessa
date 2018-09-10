#pragma once

#include <unordered_map>
#include "uint256.h"

class CBlockIndex;

struct BlockHasher {
  size_t operator()(const uint256& hash) const { return hash.GetLow64(); }
};

typedef std::unordered_map<uint256, CBlockIndex*, BlockHasher> BlockMap;

extern BlockMap mapBlockIndex;
