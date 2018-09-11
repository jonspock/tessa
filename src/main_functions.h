#pragma once

#include <cstdint>
class CTransaction;

bool IsFinalTx(const CTransaction& tx, int nBlockHeight = 0, int64_t nBlockTime = 0);
/** See whether the protocol update is enforced for connected nodes */
int ActiveProtocol();
