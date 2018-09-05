// Copyright (c) 2017-2018 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "ecdsa/key.h"
#include "libzerocoin/Denominations.h"
#include "bignum.h"
#include "serialize.h"
#include "amount.h"

// struct that is safe to store essential mint data, without holding any information that allows for actual spending
// (serial, randomness, private key)
struct CMintMeta {
  int nHeight;
  uint256 hashSerial;
  uint256 hashPubcoin;
  uint8_t nMetaVersion;
  libzerocoin::CoinDenomination denom;
  uint256 txid;
  bool isUsed;
  bool isArchived;
  bool isDeterministic;

  bool operator<(const CMintMeta& a) const;
};

uint256 GetSerialHash(const CBigNum& bnSerial);
uint256 GetPubCoinHash(const CBigNum& bnValue);

class CZerocoinMint {
 private:
  libzerocoin::CoinDenomination denomination;
  int nHeight;
  CBigNum value;
  CBigNum randomness;
  CBigNum serialNumber;
  uint256 txid;
  CPrivKey privkey;
  uint8_t nMintVersion;
  bool isUsed;

 public:
  static const int CURRENT_MINT_VERSION = 1; // No special processing

  CZerocoinMint() { SetNull(); }

  CZerocoinMint(libzerocoin::CoinDenomination denom, const CBigNum& value, const CBigNum& randomness,
                const CBigNum& serialNumber, bool isUsed, const uint8_t& nVersion, CPrivKey* privkey = nullptr) {
    SetNull();
    this->denomination = denom;
    this->value = value;
    this->randomness = randomness;
    this->serialNumber = serialNumber;
    this->isUsed = isUsed;
    this->nMintVersion = nVersion;
    if (privkey) this->privkey = *privkey;
  }

  void SetNull() {
    isUsed = false;
    randomness = 0;
    value = 0;
    denomination = libzerocoin::ZQ_ERROR;
    nHeight = 0;
    txid.SetNull();
    nMintVersion = 1;
    privkey.clear();
  }

  uint256 GetHash() const;

  CBigNum GetValue() const { return value; }
  void SetValue(const CBigNum& value) { this->value = value; }
  libzerocoin::CoinDenomination GetDenomination() const { return denomination; }
  int64_t GetDenominationAsAmount() const { return denomination * COIN; }
  void SetDenomination(libzerocoin::CoinDenomination denom) { this->denomination = denom; }
  int GetHeight() const { return nHeight; }
  void SetHeight(int nHeight) { this->nHeight = nHeight; }
  bool IsUsed() const { return this->isUsed; }
  void SetUsed(bool isUsed) { this->isUsed = isUsed; }
  CBigNum GetRandomness() const { return randomness; }
  void SetRandomness(const CBigNum& rand) { this->randomness = rand; }
  CBigNum GetSerialNumber() const { return serialNumber; }
  void SetSerialNumber(const CBigNum& serial) { this->serialNumber = serial; }
  uint256 GetTxHash() const { return this->txid; }
  void SetTxHash(uint256 txid) { this->txid = txid; }
  uint8_t GetVersion() const { return this->nMintVersion; }
  void SetVersion(const uint8_t ver) { this->nMintVersion = ver; }
  CPrivKey GetPrivKey() const { return this->privkey; }
  void SetPrivKey(const CPrivKey& privkey) { this->privkey = privkey; }
  bool GetKeyPair(ecdsa::CKey& key) const;

  inline bool operator<(const CZerocoinMint& a) const { return GetHeight() < a.GetHeight(); }

  CZerocoinMint(const CZerocoinMint& other) {
    denomination = other.GetDenomination();
    nHeight = other.GetHeight();
    value = other.GetValue();
    randomness = other.GetRandomness();
    serialNumber = other.GetSerialNumber();
    txid = other.GetTxHash();
    isUsed = other.IsUsed();
    nMintVersion = other.GetVersion();
    privkey = other.privkey;
  }

  std::string ToString() const;

  bool operator==(const CZerocoinMint& other) const { return this->GetValue() == other.GetValue(); }

  // Copy another CZerocoinMint
  inline CZerocoinMint& operator=(const CZerocoinMint& other) {
    denomination = other.GetDenomination();
    nHeight = other.GetHeight();
    value = other.GetValue();
    randomness = other.GetRandomness();
    serialNumber = other.GetSerialNumber();
    txid = other.GetTxHash();
    isUsed = other.IsUsed();
    nMintVersion = other.GetVersion();
    privkey = other.GetPrivKey();
    return *this;
  }

  // why 6 below (SPOCK)
  inline bool checkUnused(int denom, int Height) const {
    if (IsUsed() == false && GetDenomination() == denomination && GetRandomness() != 0 && GetSerialNumber() != 0 &&
        GetHeight() != -1 && GetHeight() != INT_MAX && GetHeight() >= 1 && (GetHeight() + 6 <= Height)) {
      return true;
    } else {
      return false;
    }
  }

  ADD_SERIALIZE_METHODS

  template <typename Stream, typename Operation>
  inline void SerializationOp(Stream& s, Operation ser_action) {
    READWRITE(nMintVersion);
    // Modify code below if changes needed for nMintVersion
    READWRITE(privkey);
    READWRITE(isUsed);
    READWRITE(randomness);
    READWRITE(serialNumber);
    READWRITE(value);
    READWRITE(denomination);
    READWRITE(nHeight);
    READWRITE(txid);
  }
};

class CZerocoinSpend {
 private:
  CBigNum coinSerial;
  uint256 hashTx;
  CBigNum pubCoin;
  libzerocoin::CoinDenomination denomination;
  unsigned int nAccumulatorChecksum;
  int nMintCount;  // memory only - the amount of mints that belong to the accumulator this is spent from

 public:
  CZerocoinSpend() { SetNull(); }

  CZerocoinSpend(const CBigNum& coinSerial, uint256 hashTx, const CBigNum& pubCoin, libzerocoin::CoinDenomination denomination,
                 unsigned int nAccumulatorChecksum) {
    this->coinSerial = coinSerial;
    this->hashTx = hashTx;
    this->pubCoin = pubCoin;
    this->denomination = denomination;
    this->nAccumulatorChecksum = nAccumulatorChecksum;
  }

  void SetNull() {
    coinSerial = 0;
      hashTx.SetNull();
      pubCoin=0;
    denomination = libzerocoin::ZQ_ERROR;
  }

  CBigNum GetSerial() const { return coinSerial; }
  uint256 GetTxHash() const { return hashTx; }
  void SetTxHash(uint256 hash) { this->hashTx = hash; }
  CBigNum GetPubCoin() const { return pubCoin; }
  libzerocoin::CoinDenomination GetDenomination() const { return denomination; }
  unsigned int GetAccumulatorChecksum() const { return this->nAccumulatorChecksum; }
  uint256 GetHash() const;
  void SetMintCount(int nMintsAdded) { this->nMintCount = nMintsAdded; }
  int GetMintCount() const { return nMintCount; }

  ADD_SERIALIZE_METHODS

  template <typename Stream, typename Operation>
  inline void SerializationOp(Stream& s, Operation ser_action) {
    READWRITE(coinSerial);
    READWRITE(hashTx);
    READWRITE(pubCoin);
    READWRITE(denomination);
    READWRITE(nAccumulatorChecksum);
  }
};

class CZerocoinSpendReceipt {
 private:
  std::string strStatusMessage;
  int nStatus;
  int nNeededSpends;
  std::vector<CZerocoinSpend> vSpends;

 public:
  void AddSpend(const CZerocoinSpend& spend);
  std::vector<CZerocoinSpend> GetSpends();
  void SetStatus(const std::string& strStatus, int nStatus, int nNeededSpends = 0);
  std::string GetStatusMessage();
  int GetStatus();
  int GetNeededSpends();
};

