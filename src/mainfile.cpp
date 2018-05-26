// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The ClubChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "main_externs.h"
// for "_" and AbortNode
#include "ui_interface.h"

// for StartShutdown()
#include "init.h"

/** Minimum disk space required - used in CheckDiskSpace() */
static const uint64_t nMinDiskSpace = 52428800;

#include <boost/filesystem.hpp>
#include <boost/filesystem/fstream.hpp>

using namespace boost;
using namespace std;

boost::filesystem::path GetBlockPosFilename(const CDiskBlockPos& pos, const char* prefix) {
  return GetDataDir() / "blocks" / strprintf("%s%05u.dat", prefix, pos.nFile);
}

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
  uint64_t nFreeBytesAvailable = filesystem::space(GetDataDir()).available;

  // Check for nMinDiskSpace bytes (currently 50MB)
  if (nFreeBytesAvailable < nMinDiskSpace + nAdditionalBytes)
    return AbortNode("Disk space is low!", _("Error: Disk space is low!"));

  return true;
}

FILE* OpenDiskFile(const CDiskBlockPos& pos, const char* prefix, bool fReadOnly) {
  if (pos.IsNull()) return NULL;
  boost::filesystem::path path = GetBlockPosFilename(pos, prefix);
  boost::filesystem::create_directories(path.parent_path());
  FILE* file = fopen(path.string().c_str(), "rb+");
  if (!file && !fReadOnly) file = fopen(path.string().c_str(), "wb+");
  if (!file) {
    LogPrintf("Unable to open file %s\n", path.string());
    return NULL;
  }
  if (pos.nPos) {
    if (fseek(file, pos.nPos, SEEK_SET)) {
      LogPrintf("Unable to seek to position %u of %s\n", pos.nPos, path.string());
      fclose(file);
      return NULL;
    }
  }
  return file;
}

FILE* OpenBlockFile(const CDiskBlockPos& pos, bool fReadOnly) { return OpenDiskFile(pos, "blk", fReadOnly); }

FILE* OpenUndoFile(const CDiskBlockPos& pos, bool fReadOnly) { return OpenDiskFile(pos, "rev", fReadOnly); }

CBlockIndex* InsertBlockIndex(uint256 hash) {
  if (hash == 0) return NULL;

  // Return existing
  BlockMap::iterator mi = mapBlockIndex.find(hash);
  if (mi != mapBlockIndex.end()) return (*mi).second;

  // Create new
  CBlockIndex* pindexNew = new CBlockIndex();
  if (!pindexNew) throw runtime_error("LoadBlockIndex() : new CBlockIndex failed");
  mi = mapBlockIndex.insert(make_pair(hash, pindexNew)).first;

  // mark as PoS seen
  if (pindexNew->IsProofOfStake()) setStakeSeen.insert(make_pair(pindexNew->prevoutStake, pindexNew->nStakeTime));

  pindexNew->phashBlock = &((*mi).first);

  return pindexNew;
}
