#pragma once
#include "primitives/block.h"
#include "primitives/transaction.h"

class CWallet;
class CBlock;
class CBlockIndex;

// MODIFIER_INTERVAL: time to elapse before new modifier is computed

// MODIFIER_INTERVAL_RATIO:
// ratio of group interval length between the last group and the first group
static const int MODIFIER_INTERVAL_RATIO = 3;

class CStaker {
 public:
  CStaker() {
    nLastCoinStakeSearchInterval = 0;
    init = false;
  }

  void setLastCoinStakeSearchInterval(int64_t t) { nLastCoinStakeSearchInterval = t; }
  void setLastCoinStakeSearchTime(int64_t t) { nLastCoinStakeSearchTime = t; }
  int64_t getLastCoinStakeSearchInterval() { return nLastCoinStakeSearchInterval; }
  int64_t getLastCoinStakeSearchTime() { return nLastCoinStakeSearchTime; }

  void Setup(int64_t value) {
    if (!init) nLastCoinStakeSearchTime = value;
    init = true;
  }

  // Only set, never used????
  void setSeen(const std::pair<COutPoint, unsigned int>& v) { setStakeSeen.insert(v); }

  bool FindStake(int64_t time, CBlockIndex* Tip, CBlock* pblock, CWallet* pwallet);

 private:
  std::set<std::pair<COutPoint, unsigned int> > setStakeSeen;
  int64_t nLastCoinStakeSearchTime;
  int64_t nLastCoinStakeSearchInterval;
  // unsigned int nModifierInterval;
  bool init;
};

extern CStaker gStaker;
