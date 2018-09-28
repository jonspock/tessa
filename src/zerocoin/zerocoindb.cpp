// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2016-2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "zerocoindb.h"
#include "primitives/zerocoin.h"
#include "fs.h"
#include "fs_utils.h"
#include "libzerocoin/CoinSpend.h"
#include "uint256.h"
#include <cstdint>

using namespace std;

CZerocoinDB::CZerocoinDB(size_t nCacheSize, bool fMemory, bool fWipe)
    : CDataDBWrapper(GetDataDir() / "zerocoin", nCacheSize, fMemory, fWipe) {}

void CZerocoinDB::InterruptWipeCoins() { interrupt = true; }

bool CZerocoinDB::WriteCoinMint(const libzerocoin::PublicCoin& pubCoin, const uint256& hashTx) {
  uint256 hash = GetPubCoinHash(pubCoin.getValue());
  return Write(make_pair('m', hash), hashTx, true);
}
bool CZerocoinDB::WriteCoinMintBatch(const std::vector<std::pair<libzerocoin::PublicCoin, uint256> >& mintInfo) {
  CDataDBBatch batch;
  size_t count = 0;
  if (mintInfo.size() == 0) return true;
  for (auto& it : mintInfo) {
    libzerocoin::PublicCoin pubCoin = it.first;
    uint256 hash = GetPubCoinHash(pubCoin.getValue());
    batch.Write(make_pair('m', hash), it.second);
    ++count;
  }

  LogPrint(TessaLog::ZKP, "Writing %u coin mints to db.\n", count);
  return WriteBatch(batch, true);
}

bool CZerocoinDB::ReadCoinMint(const CBigNum& bnPubcoin, uint256& hashTx) {
  return ReadCoinMint(GetPubCoinHash(bnPubcoin), hashTx);
}

bool CZerocoinDB::ReadCoinMint(const uint256& hashPubcoin, uint256& hashTx) {
  return Read(make_pair('m', hashPubcoin), hashTx);
}

bool CZerocoinDB::EraseCoinMint(const CBigNum& bnPubcoin) {
  uint256 hash = GetPubCoinHash(bnPubcoin);
  return Erase(make_pair('m', hash));
}

bool CZerocoinDB::WriteCoinSpend(const CBigNum& bnSerial, const uint256& txHash) {
  CDataStream ss(SER_GETHASH);
  ss << bnSerial;
  uint256 hash = Hash(ss.begin(), ss.end());

  return Write(make_pair('s', hash), txHash, true);
}
/*
bool CZerocoinDB::WriteCoinSpendBatch(const std::vector<std::pair<libzerocoin::CoinSpend, uint256> >& spendInfo) {
  CDataDBBatch batch;
  size_t count = 0;
  for (auto& it : spendInfo) {
    CBigNum bnSerial = it.first.getCoinSerialNumber();
    CDataStream ss(SER_GETHASH, 0);
    ss << bnSerial;
    uint256 hash = Hash(ss.begin(), ss.end());
    batch.Write(make_pair('s', hash), it.second);
    ++count;
  }

  LogPrint(TessaLog::ZKP, "Writing %u coin spends to db.\n", count);
  return WriteBatch(batch, true);
}
*/
bool CZerocoinDB::ReadCoinSpend(const CBigNum& bnSerial, uint256& txHash) {
  CDataStream ss(SER_GETHASH);
  ss << bnSerial;
  uint256 hash = Hash(ss.begin(), ss.end());

  return Read(make_pair('s', hash), txHash);
}

bool CZerocoinDB::ReadCoinSpend(const uint256& hashSerial, uint256& txHash) {
  return Read(make_pair('s', hashSerial), txHash);
}

bool CZerocoinDB::EraseCoinSpend(const CBigNum& bnSerial) {
  CDataStream ss(SER_GETHASH);
  ss << bnSerial;
  uint256 hash = Hash(ss.begin(), ss.end());

  return Erase(make_pair('s', hash));
}

bool CZerocoinDB::WipeCoins(const std::string& strType) {
  if (strType != "spends" && strType != "mints") return error("%s: did not recognize type %s", __func__, strType);

  std::unique_ptr<datadb::Iterator> pcursor(NewIterator());

  char type = (strType == "spends" ? 's' : 'm');
  CDataStream ssKeySet(SER_DISK, CLIENT_VERSION);
  ssKeySet << make_pair(type, uint256());
  pcursor->Seek(ssKeySet.str());
  // Load mapBlockIndex
  std::set<uint256> setDelete;
  while (pcursor->Valid()) {
    if (interrupt) return error("WipeCoins() : interrupted");
    try {
      datadb::Slice slKey = pcursor->key();
      CDataStream ssKey(slKey.data(), slKey.data() + slKey.size(), SER_DISK, CLIENT_VERSION);
      char chType;
      ssKey >> chType;
      if (chType == type) {
        datadb::Slice slValue = pcursor->value();
        CDataStream ssValue(slValue.data(), slValue.data() + slValue.size(), SER_DISK, CLIENT_VERSION);
        uint256 hash;
        ssValue >> hash;
        setDelete.insert(hash);
        pcursor->Next();
      } else {
        break;  // if shutdown requested or finished loading block index
      }
    } catch (std::exception& e) { return error("%s : Deserialize or I/O error - %s", __func__, e.what()); }
  }

  for (auto& hash : setDelete) {
    if (!Erase(make_pair(type, hash))) LogPrintf("%s: error failed to delete %s\n", __func__, hash.GetHex());
  }

  return true;
}

bool CZerocoinDB::WriteAccumulatorValue(const uint32_t& nChecksum, const CBigNum& bnValue) {
  return Write(make_pair('2', nChecksum), bnValue);
}

bool CZerocoinDB::ReadAccumulatorValue(const uint32_t& nChecksum, CBigNum& bnValue) {
  return Read(make_pair('2', nChecksum), bnValue);
}

bool CZerocoinDB::EraseAccumulatorValue(const uint32_t& nChecksum) {
  LogPrint(TessaLog::ZKP, "%s : checksum:%d\n", __func__, nChecksum);
  return Erase(make_pair('2', nChecksum));
}
