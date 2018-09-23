// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "datadbwrapper.h"
#include "spork.h"

class CSporkDB : public CDataDBWrapper {
 public:
  CSporkDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

 private:
  CSporkDB(const CSporkDB&);
  void operator=(const CSporkDB&);

 public:
  bool WriteSpork(const SporkID nSporkId, const CSporkMessage& spork);
  bool ReadSpork(const SporkID nSporkId, CSporkMessage& spork);
  bool SporkExists(const SporkID nSporkId);
};
