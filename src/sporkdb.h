// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "datadbwrapper.h"
#include "spork.h"

class CSporkDB : public CDataDBWrapper {
 public:
  CSporkDB();

 public:
  bool WriteSpork(const SporkID nSporkId, const CSporkMessage& spork) {
    LogPrint(TessaLog::SPORK, "Wrote spork %s to database\n", gSporkManager.GetSporkNameByID(nSporkId));
    return Write((int)nSporkId, spork);
  }
  bool ReadSpork(const SporkID nSporkId, CSporkMessage& spork) { return Read((int)nSporkId, spork); }
  bool SporkExists(const SporkID nSporkId) { return Exists((int)nSporkId); }
};
