// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2016-2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "fs.h"
#include "fs_utils.h"
#include "main.h"
#include "txdb.h"
#include "uint256.h"
#include <cstdint>

using namespace std;

void static BatchWriteCoins(CDataDBBatch& batch, const uint256& hash, const CCoins& coins) {
  if (coins.IsPruned())
    batch.Erase(make_pair('c', hash));
  else
    batch.Write(make_pair('c', hash), coins);
}

void static BatchWriteHashBestChain(CDataDBBatch& batch, const uint256& hash) { batch.Write('B', hash); }

CCoinsViewDB::CCoinsViewDB(size_t nCacheSize, bool fMemory, bool fWipe)
    : db(GetDataDir() / "chainstate", nCacheSize, fMemory, fWipe) {}

bool CCoinsViewDB::GetCoins(const uint256& txid, CCoins& coins) const { return db.Read(make_pair('c', txid), coins); }

bool CCoinsViewDB::HaveCoins(const uint256& txid) const { return db.Exists(make_pair('c', txid)); }

uint256 CCoinsViewDB::GetBestBlock() const {
  uint256 hashBestChain;
  uint256 z;
  z.SetNull();
  if (!db.Read('B', hashBestChain)) return z;
  return hashBestChain;
}

bool CCoinsViewDB::BatchWrite(CCoinsMap& mapCoins, const uint256& hashBlock) {
  CDataDBBatch batch;
  size_t count = 0;
  size_t changed = 0;
  for (const auto& it : mapCoins) {
    if (it.second.flags & CCoinsCacheEntry::DIRTY) {
      BatchWriteCoins(batch, it.first, it.second.coins);
      changed++;
    }
    count++;
  }
  mapCoins.clear();
  if (!hashBlock.IsNull()) BatchWriteHashBestChain(batch, hashBlock);

  LogPrint(TessaLog::COINDB, "Committing %u changed transactions (out of %u) to coin database...\n", changed, count);
  return db.WriteBatch(batch);
}

bool CCoinsViewDB::GetStats(CCoinsStats& stats) const {
  /* It seems that there are no "const iterators" for DataDB.  Since we
     only need read operations on it, use a const-cast to get around
     that restriction.  */
  std::unique_ptr<datadb::Iterator> pcursor(const_cast<CDataDBWrapper*>(&db)->NewIterator());
  pcursor->SeekToFirst();

  CHashWriter ss;
  stats.hashBlock = GetBestBlock();
  ss << stats.hashBlock;
  CAmount nTotalAmount = 0;
  while (pcursor->Valid()) {
    if (interrupt) return error("GetStats() : interrupted");
    try {
      datadb::Slice slKey = pcursor->key();
      CDataStream ssKey(slKey.data(), slKey.data() + slKey.size(), SER_DISK, CLIENT_VERSION);
      char chType;
      ssKey >> chType;
      if (chType == 'c') {
        datadb::Slice slValue = pcursor->value();
        CDataStream ssValue(slValue.data(), slValue.data() + slValue.size(), SER_DISK, CLIENT_VERSION);
        CCoins coins;
        ssValue >> coins;
        uint256 txhash;
        ssKey >> txhash;
        ss << txhash;
        ss << VARINT(coins.nTransactionVersion);
        ss << (coins.fCoinBase ? 'c' : 'n');
        ss << VARINT(coins.nHeight);
        stats.nTransactions++;
        for (size_t i = 0; i < coins.vout.size(); i++) {
          const CTxOut& out = coins.vout[i];
          if (!out.IsNull()) {
            stats.nTransactionOutputs++;
            ss << VARINT(i + 1);
            ss << out;
            nTotalAmount += out.nValue;
          }
        }
        stats.nSerializedSize += 32 + slValue.size();
        ss << VARINT(0);
      }
      pcursor->Next();
    } catch (std::exception& e) { return error("%s : Deserialize or I/O error - %s", __func__, e.what()); }
  }
  stats.nHeight = mapBlockIndex.find(GetBestBlock())->second->nHeight;
  stats.hashSerialized = ss.GetHash();
  stats.nTotalAmount = nTotalAmount;
  return true;
}

void CCoinsViewDB::InterruptGetStats() { interrupt = true; }
