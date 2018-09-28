// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2016-2018 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "datadbwrapper.h"
#include "bignum.h"

#include <string>
#include <vector>

class uint256;
namespace libzerocoin {
class PublicCoin;
}

class CZerocoinDB : public CDataDBWrapper {
 public:
  CZerocoinDB(size_t nCacheSize, bool fMemory = false, bool fWipe = false);

 private:
  CZerocoinDB(const CZerocoinDB&);
  void operator=(const CZerocoinDB&);
  std::atomic<bool> interrupt = false;

 public:
  bool WriteCoinMintBatch(const std::vector<std::pair<libzerocoin::PublicCoin, uint256> >& mintInfo);
  //  bool WriteCoinSpendBatch(const std::vector<std::pair<libzerocoin::CoinSpend, uint256> >& spendInfo);
  bool WriteCoinMint(const libzerocoin::PublicCoin& pubCoin, const uint256& txHash);
  bool ReadCoinMint(const CBigNum& bnPubcoin, uint256& txHash);
  bool ReadCoinMint(const uint256& hashPubcoin, uint256& hashTx);
  bool WriteCoinSpend(const CBigNum& bnSerial, const uint256& txHash);
  bool ReadCoinSpend(const CBigNum& bnSerial, uint256& txHash);
  bool ReadCoinSpend(const uint256& hashSerial, uint256& txHash);
  bool EraseCoinMint(const CBigNum& bnPubcoin);
  bool EraseCoinSpend(const CBigNum& bnSerial);
  bool WipeCoins(const std::string& strType);
  bool WriteAccumulatorValue(const uint32_t& nChecksum, const CBigNum& bnValue);
  bool ReadAccumulatorValue(const uint32_t& nChecksum, CBigNum& bnValue);
  bool EraseAccumulatorValue(const uint32_t& nChecksum);
  void InterruptWipeCoins();
};
