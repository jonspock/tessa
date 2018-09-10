// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockdisk.h"

#include "addrman.h"
#include "chainparams.h"
#include "checkqueue.h"
#include "ecdsa/blocksignature.h"
#include "init.h"
#include "kernel.h"
#include "main.h"
#include "merkleblock.h"
#include "net.h"
#include "txdb.h"
#include "txmempool.h"
#include "util.h"

#include <sstream>

using namespace std;

//////////////////////////////////////////////////////////////////////////////
//
// CBlock and CBlockIndex
//

bool WriteBlockToDisk(CBlock& block, CDiskBlockPos& pos) {
  // Open history file to append
  CAutoFile fileout(OpenBlockFile(pos), SER_DISK, CLIENT_VERSION);
  if (fileout.IsNull()) return error("WriteBlockToDisk : OpenBlockFile failed");

  // Write index header
  uint32_t nSize = fileout.GetSerializeSize(block);
  fileout << FLATDATA(Params().MessageStart()) << nSize;

  // Write block
  int64_t fileOutPos = ftell(fileout.Get());
  if (fileOutPos < 0) return error("WriteBlockToDisk : ftell failed");
  pos.nPos = (uint32_t)fileOutPos;
  fileout << block;

  return true;
}

bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos) {
  block.SetNull();

  // Open history file to read
  CAutoFile filein(OpenBlockFile(pos, true), SER_DISK, CLIENT_VERSION);
  if (filein.IsNull()) return error("ReadBlockFromDisk : OpenBlockFile failed");

  // Read block
  try {
    filein >> block;
  } catch (std::exception& e) { return error("%s : Deserialize or I/O error - %s", __func__, e.what()); }

  // Check the header
  if (block.IsProofOfWork()) {
    if (!CheckProofOfWork(block.GetHash(), block.nBits)) return error("ReadBlockFromDisk : Errors in block header");
  }

  return true;
}

bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex) {
  if (!ReadBlockFromDisk(block, pindex->GetBlockPos())) return false;
  if (block.GetHash() != pindex->GetBlockHash()) {
    LogPrintf("%s : block=%s index=%s\n", __func__, block.GetHash().ToString().c_str(),
              pindex->GetBlockHash().ToString().c_str());
    return error("ReadBlockFromDisk(CBlock&, CBlockIndex*) : GetHash() doesn't match index");
  }
  return true;
}

double ConvertBitsToDouble(uint32_t nBits) {
  int nShift = (nBits >> 24) & 0xff;

  double dDiff = (double)0x0000ffff / (double)(nBits & 0x00ffffff);

  while (nShift < 29) {
    dDiff *= 256.0;
    nShift++;
  }
  while (nShift > 29) {
    dDiff /= 256.0;
    nShift--;
  }

  return dDiff;
}

int64_t GetBlockValue(int nHeight) {
  int64_t nSubsidy = 5000 * COIN;  // for now XXX HACK
  return nSubsidy;
}

bool IsInitialBlockDownload() {
  LOCK(cs_main);
  if (fImporting || fReindex || fVerifyingBlocks || chainActive.Height() < Checkpoints::GetTotalBlocksEstimate())
    return true;
  static bool lockIBDState = false;
  if (lockIBDState) return false;
  bool state =
      (chainActive.Height() < pindexBestHeader->nHeight - 24 * 6 ||
       pindexBestHeader->GetBlockTime() < GetTime() - 6 * 60 * 60);  // ~144 blocks behind -> 2 x fork detection time
  if (!state) lockIBDState = true;
  return state;
}
