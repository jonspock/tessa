#include "staker.h"
#include "wallet.h"

CStaker gStaker;

bool CStaker::FindStake(int64_t time, CBlockIndex* pindexPrev, CBlock* pblock, CWallet* pwallet) {
    pblock->nTime = time;
    pblock->nBits = GetNextWorkRequired(pindexPrev, pblock);
    CMutableTransaction txCoinStake;
    int64_t nSearchTime = pblock->nTime;  // search to current time
    bool fStakeFound = false;
    if (nSearchTime >= getLastCoinStakeSearchTime()) {
      unsigned int nTxNewTime = 0;
      if (pwallet->CreateCoinStake(*pwallet, pblock->nBits, nSearchTime - getLastCoinStakeSearchTime(), txCoinStake,
                                   nTxNewTime)) {
          pblock->nTime = nTxNewTime;
          pblock->vtx[0].vout[0].SetEmpty();
          pblock->vtx.push_back(CTransaction(txCoinStake));
          fStakeFound = true;
      }
      setLastCoinStakeSearchInterval(nSearchTime - getLastCoinStakeSearchTime());
      setLastCoinStakeSearchTime(nSearchTime);
    }
    return fStakeFound;
}
