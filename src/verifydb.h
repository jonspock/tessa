// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#pragma once

#include "coins.h"
#include <atomic>

/** RAII wrapper for VerifyDB: Verify consistency of the block and coin databases */
class CVerifyDB {
 public:
  CVerifyDB();
  ~CVerifyDB();
  bool VerifyDB(CCoinsView* coinsview, int nCheckLevel, int nCheckDepth);
  std::atomic<bool> interrupt = false;
  void InterruptInit();
};
