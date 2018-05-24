#pragma once
#include "primitives/transaction.h"

/** Threshold for nLockTime: below this value it is interpreted as block number, otherwise as UNIX timestamp. */
static const unsigned int LOCKTIME_THRESHOLD = 500000000; // Tue Nov  5 00:53:20 1985 UTC

bool IsFinalTx(const CTransaction& tx, int nBlockHeight=0, int64_t nBlockTime=0);
