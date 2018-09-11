// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "pow.h"

#include "chain.h"
#include "chainparams.h"
#include "primitives/block.h"
#include "uint256.h"
#include "util.h"

#include <cmath>

uint32_t GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader* pblock) {
  /* current difficulty formula, tessa - DarkGravity v3, written by Evan Duffield - evan@dashpay.io */
  const CBlockIndex* BlockLastSolved = pindexLast;
  const CBlockIndex* BlockReading = pindexLast;
  int64_t nActualTimespan = 0;
  int64_t LastBlockTime = 0;
  int64_t PastBlocksMin = 24;
  int64_t PastBlocksMax = 24;
  int64_t CountBlocks = 0;
  arith_uint256 PastDifficultyAverage;
  arith_uint256 PastDifficultyAveragePrev;

  // Quick Exit for testnet for Now XXX
#warning "Quick exit for testnet"
  if (pindexLast->nHeight < Params().LAST_POW_BLOCK() && Params().AllowMinDifficultyBlocks()) {
    return Params().ProofOfWorkLimit().GetCompact();
  }

  if (BlockLastSolved == nullptr || BlockLastSolved->nHeight == 0 || BlockLastSolved->nHeight < PastBlocksMin) {
    return Params().ProofOfWorkLimit().GetCompact();
  }

  if (pindexLast->nHeight > Params().LAST_POW_BLOCK()) {
    arith_uint256 bnTargetLimit = (~arith_uint256(0) >> 24);
    int64_t nTargetSpacing = 60;
    int64_t nTargetTimespan = 60 * 40;

    int64_t nActualSpacing = 0;
    if (pindexLast->nHeight != 0) nActualSpacing = pindexLast->GetBlockTime() - pindexLast->pprev->GetBlockTime();

    if (nActualSpacing < 0) nActualSpacing = 1;

    // ppcoin: target change every block
    // ppcoin: retarget with exponential moving toward target spacing
    arith_uint256 bnNew;
    bnNew.SetCompact(pindexLast->nBits);

    int64_t nInterval = nTargetTimespan / nTargetSpacing;
    bnNew *= ((nInterval - 1) * nTargetSpacing + nActualSpacing + nActualSpacing);
    bnNew /= ((nInterval + 1) * nTargetSpacing);

    if (bnNew <= 0 || bnNew > bnTargetLimit) bnNew = bnTargetLimit;

    return bnNew.GetCompact();
  }

  for (uint32_t i = 1; BlockReading && BlockReading->nHeight > 0; i++) {
    if (PastBlocksMax > 0 && i > PastBlocksMax) { break; }
    CountBlocks++;

    if (CountBlocks <= PastBlocksMin) {
      if (CountBlocks == 1) {
        PastDifficultyAverage.SetCompact(BlockReading->nBits);
      } else {
        PastDifficultyAverage =
            ((PastDifficultyAveragePrev * CountBlocks) + (arith_uint256().SetCompact(BlockReading->nBits))) /
            (CountBlocks + 1);
      }
      PastDifficultyAveragePrev = PastDifficultyAverage;
    }

    if (LastBlockTime > 0) {
      int64_t Diff = (LastBlockTime - BlockReading->GetBlockTime());
      nActualTimespan += Diff;
    }
    LastBlockTime = BlockReading->GetBlockTime();

    if (BlockReading->pprev == nullptr) {
      assert(BlockReading);
      break;
    }
    BlockReading = BlockReading->pprev;
  }

  arith_uint256 bnNew(PastDifficultyAverage);

  int64_t _nTargetTimespan = CountBlocks * Params().TargetSpacing();

  if (nActualTimespan < _nTargetTimespan / 3) nActualTimespan = _nTargetTimespan / 3;
  if (nActualTimespan > _nTargetTimespan * 3) nActualTimespan = _nTargetTimespan * 3;

  // Retarget
  bnNew *= nActualTimespan;
  bnNew /= _nTargetTimespan;

  if (bnNew > Params().ProofOfWorkLimit()) { bnNew = Params().ProofOfWorkLimit(); }

  return bnNew.GetCompact();
}

bool CheckProofOfWork(uint256 hash, uint32_t nBits) {
  bool fNegative;
  bool fOverflow;
  arith_uint256 bnTarget;

  if (Params().SkipProofOfWorkCheck()) return true;

  bnTarget.SetCompact(nBits, &fNegative, &fOverflow);

  // Check range
  if (fNegative || bnTarget == 0 || fOverflow || bnTarget > Params().ProofOfWorkLimit())
    return error("CheckProofOfWork() : nBits below minimum work");

  // Check proof of work matches claimed amount
  std::cout << "hash = \n" << hash.ToString() << " vs " << bnTarget.ToString() << "\n";
  if (UintToArith256(hash) > bnTarget) return error("CheckProofOfWork() : hash doesn't match nBits");

  return true;
}

arith_uint256 GetBlockProof(const CBlockIndex& block) {
  arith_uint256 bnTarget;
  bool fNegative;
  bool fOverflow;
  bnTarget.SetCompact(block.nBits, &fNegative, &fOverflow);
  if (fNegative || fOverflow || bnTarget == 0) return 0;
  // We need to compute 2**256 / (bnTarget+1), but we can't represent 2**256
  // as it's too large for a uint256. However, as 2**256 is at least as large
  // as bnTarget+1, it is equal to ((2**256 - bnTarget - 1) / (bnTarget+1)) + 1,
  // or ~bnTarget / (nTarget+1) + 1.
  return (~bnTarget / (bnTarget + 1)) + 1;
}
