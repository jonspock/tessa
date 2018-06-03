// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 The ClubChain developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "sporkdb.h"
#include "spork.h"
#include <boost/filesystem/path.hpp>

CSporkDB::CSporkDB(size_t nCacheSize, bool fMemory, bool fWipe)
    : CLevelDBWrapper(GetDataDir() / "sporks", nCacheSize, fMemory, fWipe) {}

bool CSporkDB::WriteSpork(const SporkID nSporkId, const CSporkMessage& spork) {
  LogPrintf("Wrote spork %s to database\n", gSporkManager.GetSporkNameByID(nSporkId));
  return Write((int)nSporkId, spork);
}

bool CSporkDB::ReadSpork(const SporkID nSporkId, CSporkMessage& spork) { return Read((int)nSporkId, spork); }

bool CSporkDB::SporkExists(const SporkID nSporkId) { return Exists((int)nSporkId); }
