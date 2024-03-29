// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "blockundo.h"
#include "chainparams.h"
#include "clientversion.h"
#include "hash.h"
#include "mainfile.h"
#include "streams.h"
#include <sstream>

using namespace std;

bool CBlockUndo::WriteToDisk(CDiskBlockPos& pos, const uint256& hashBlock) {
  // Open history file to append
  CAutoFile fileout(OpenUndoFile(pos), SER_DISK, CLIENT_VERSION);
  if (fileout.IsNull()) return error("CBlockUndo::WriteToDisk : OpenUndoFile failed");

  // Write index header
  uint32_t nSize = fileout.GetSerializeSize(*this);
  fileout << FLATDATA(Params().MessageStart()) << nSize;

  // Write undo data
  int64_t fileOutPos = ftell(fileout.Get());
  if (fileOutPos < 0) return error("CBlockUndo::WriteToDisk : ftell failed");
  pos.nPos = (uint32_t)fileOutPos;
  fileout << *this;

  // calculate & write checksum
  CHashWriter hasher;
  hasher << hashBlock;
  hasher << *this;
  fileout << hasher.GetHash();

  return true;
}

bool CBlockUndo::ReadFromDisk(const CDiskBlockPos& pos, const uint256& hashBlock) {
  // Open history file to read
  CAutoFile filein(OpenUndoFile(pos, true), SER_DISK, CLIENT_VERSION);
  if (filein.IsNull()) return error("CBlockUndo::ReadFromDisk : OpenBlockFile failed");

  // Read block
  uint256 hashChecksum;
  try {
    filein >> *this;
    filein >> hashChecksum;
  } catch (std::exception& e) { return error("%s : Deserialize or I/O error - %s", __func__, e.what()); }

  // Verify checksum
  CHashWriter hasher;
  hasher << hashBlock;
  hasher << *this;
  if (hashChecksum != hasher.GetHash()) return error("CBlockUndo::ReadFromDisk : Checksum mismatch");

  return true;
}
