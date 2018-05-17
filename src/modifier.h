// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2012-2017 The Pivx developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_MODIFIER_H
#define BITCOIN_MODIFIER_H

#include "chain.h"

bool ComputeNextStakeModifier(const CBlockIndex* pindexPrev, uint64_t& nStakeModifier, bool& fGeneratedStakeModifier);
unsigned int GetStakeModifierChecksum(const CBlockIndex* pindex);
bool CheckStakeModifierCheckpoints(int nHeight, unsigned int nStakeModifierChecksum);
uint64_t ModifierFromBlockHash(uint256 hashBlockFrom);
#endif //BITCOIN_MODIFIER_H
