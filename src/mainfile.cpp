// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockmap.h"
#include "net.h"  // all needed?
#include "uint256.h"
// for "_"
#include "util.h"
// for AbortNode
#include "chain.h"
#include "fs.h"
#include "fs_utils.h"
#include "ui_interface.h"
// for StartShutdown()
#include "init.h"
#include "staker.h"

using namespace std;

fs::path GetBlockPosFilename(const CDiskBlockPos& pos, const char* prefix) {
  return GetDataDir() / "blocks" / strprintf("%s%05u.dat", prefix, pos.nFile);
}
fs::path GetBlockPosFilenameDir() { return GetDataDir() / "blocks"; }

bool AbortNode(const std::string& strMessage, const std::string& userMessage) {
  strMiscWarning = strMessage;
  LogPrintf("*** %s\n", strMessage);
  uiInterface.ThreadSafeMessageBox(
      userMessage.empty() ? _("Error: A fatal internal error occured, see debug.log for details") : userMessage, "",
      CClientUIInterface::MSG_ERROR);
  StartShutdown();
  return false;
}

bool CheckDiskSpace(uint64_t nAdditionalBytes) {
#ifndef NO_BOOST_FILESYSTEM
  /** Minimum disk space required - used in CheckDiskSpace() */
  const uint64_t nMinDiskSpace = 52428800;
  uint64_t nFreeBytesAvailable = fs::space(GetDataDir()).available;

  // Check for nMinDiskSpace bytes (currently 50MB)
  if (nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes)
    return AbortNode("Disk space is low!", _("Error: Disk space is low!"));
#endif
  return true;
}

FILE* OpenDiskFile(const CDiskBlockPos& pos, const char* prefix, bool fReadOnly) {
  if (pos.IsNull()) return nullptr;
  fs::path path = GetBlockPosFilename(pos, prefix);
  fs::create_directories(GetBlockPosFilenameDir());
  FILE* file = fopen(path.string().c_str(), "rb+");
  if (!file && !fReadOnly) file = fopen(path.string().c_str(), "wb+");
  if (!file) {
    LogPrintf("Unable to open file %s\n", path.string());
    return nullptr;
  }
  if (pos.nPos) {
    if (fseek(file, pos.nPos, SEEK_SET)) {
      LogPrintf("Unable to seek to position %u of %s\n", pos.nPos, path.string());
      fclose(file);
      return nullptr;
    }
  }
  return file;
}

FILE* OpenBlockFile(const CDiskBlockPos& pos, bool fReadOnly) { return OpenDiskFile(pos, "blk", fReadOnly); }

FILE* OpenUndoFile(const CDiskBlockPos& pos, bool fReadOnly) { return OpenDiskFile(pos, "rev", fReadOnly); }

CBlockIndex* InsertBlockIndex(uint256 hash) {
  if (hash.IsNull()) return nullptr;

  // Return existing
  BlockMap::iterator mi = mapBlockIndex.find(hash);
  if (mi != mapBlockIndex.end()) return (*mi).second;

  // Create new
  CBlockIndex* pindexNew = new CBlockIndex();
  if (!pindexNew) throw runtime_error("LoadBlockIndex() : new CBlockIndex failed");
  mi = mapBlockIndex.insert(make_pair(hash, pindexNew)).first;

  // mark as PoS seen
  if (pindexNew->IsProofOfStake()) gStaker.setSeen(make_pair(pindexNew->prevoutStake, pindexNew->nStakeTime));

  pindexNew->phashBlock = &((*mi).first);

  return pindexNew;
}
