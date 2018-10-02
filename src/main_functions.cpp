#include "main_functions.h"
#include "chain.h"
#include "coin_constants.h"  // for LOCKTIME_THRESHOLD
#include "coin_externs.h"
#include "timedata.h"

bool IsFinalTx(const CTransaction& tx, int nBlockHeight, int64_t nBlockTime) {
  AssertLockHeld(cs_main);
  // Time based nLockTime implemented in 0.1.6
  if (tx.nLockTime == 0) return true;
  if (nBlockHeight == 0) nBlockHeight = chainActive.Height();
  if (nBlockTime == 0) nBlockTime = GetAdjustedTime();
  if ((int64_t)tx.nLockTime < ((int64_t)tx.nLockTime < LOCKTIME_THRESHOLD ? (int64_t)nBlockHeight : nBlockTime))
    return true;
  for (const CTxIn& txin : tx.vin)
    if (!txin.IsFinal()) return false;
  return true;
}
