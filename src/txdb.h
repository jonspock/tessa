// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2016-2018 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "blockfileinfo.h"
#include "blockmap.h"
#include "coins.h"
#include "datadbwrapper.h"
#include "disktxpos.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

class CCoins;
class uint256;

//! -dbcache default (MiB)
static const int64_t nDefaultDbCache = 100;
//! max. -dbcache in (MiB)
static const int64_t nMaxDbCache = sizeof(void*) > 4 ? 4096 : 1024;
//! min. -dbcache in (MiB)
static const int64_t nMinDbCache = 4;

/** CCoinsView backed by the DataDB coin database (chainstate/) */
class CCoinsViewDB : public CCoinsView {
 protected:
  CDataDBWrapper db;
  std::atomic<bool> interrupt = false;

 public:
  CCoinsViewDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

  bool GetCoins(const uint256& txid, CCoins& coins) const;
  bool HaveCoins(const uint256& txid) const;
  uint256 GetBestBlock() const;
  bool BatchWrite(CCoinsMap& mapCoins, const uint256& hashBlock);
  bool GetStats(CCoinsStats& stats) const;
  void InterruptGetStats();
};

/** Access to the block database (blocks/index/) */
class CBlockTreeDB : public CDataDBWrapper {
 public:
  CBlockTreeDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

 private:
  CBlockTreeDB(const CBlockTreeDB&);
  void operator=(const CBlockTreeDB&);
  std::atomic<bool> interrupt = false;

 public:
  bool WriteBlockIndex(const CDiskBlockIndex& blockindex);
  bool ReadBlockFileInfo(int nFile, CBlockFileInfo& fileinfo);
  bool WriteBlockFileInfo(int nFile, const CBlockFileInfo& fileinfo);
  int ReadLastBlockFile();
  bool WriteLastBlockFile(int nFile);
  bool WriteReindexing(bool fReindex);
  bool ReadReindexing();
  bool ReadTxIndex(const uint256& txid, CDiskTxPos& pos);
  bool WriteTxIndex(const std::vector<std::pair<uint256, CDiskTxPos> >& list);
  bool WriteFlag(const std::string& name, bool fValue);
  bool ReadFlag(const std::string& name);
  bool LoadBlockIndexGuts();
  void InterruptLoadBlockIndexGuts();
};
