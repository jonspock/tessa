/**
 * @file       PrivateCoin.h
 *
 * @brief      PublicCoin and PrivateCoin classes for the Zerocoin library.
 *
 * @author     Ian Miers, Christina Garman and Matthew Green
 * @date       June 2013
 *
 * @copyright  Copyright 2013 Ian Miers, Christina Garman and Matthew Green
 *  license    This project is released under the MIT license.
 **/
// Copyright (c) 2018 The PIVX developer
// Copyright (c) 2018 The ClubChain developers
#pragma once
#include "Denominations.h"
#include "PublicCoin.h"
#include "ZerocoinParams.h"
#include "amount.h"
#include "bignum.h"
#include "key.h"
#include "util.h"

namespace libzerocoin {

bool GenerateKeyPair(const CBigNum& bnGroupOrder, const uint256& nPrivkey, CKey& key, CBigNum& bnSerial);

/**
 * A private coin. As the name implies, the content
 * of this should stay private except PublicCoin.
 *
 * Contains a coin's serial number, a commitment to it,
 * and opening randomness for the commitment.
 *
 * @warning Failure to keep this secret(or safe),
 * @warning will result in the theft of your coins and a TOTAL loss of anonymity.
 */
class PrivateCoin {
 public:
  template <typename Stream> PrivateCoin(const ZerocoinParams* p, Stream& strm) : params(p), publicCoin(p) { strm >> *this; }
  PrivateCoin(const ZerocoinParams* p);
  //  PrivateCoin(const ZerocoinParams* p, const CoinDenomination denomination);
  PrivateCoin(const ZerocoinParams* p, const CoinDenomination denomination, const CBigNum Serial,
              const CBigNum Randonmess);

  CBigNum CoinFromSeed(const uint512& seedZerocoin);
  
  const PublicCoin& getPublicCoin() const { return this->publicCoin; }
  // @return the coins serial number
  const CBigNum& getSerialNumber() const { return this->serialNumber; }
  const CBigNum& getRandomness() const { return this->randomness; }
  const uint8_t& getVersion() const { return this->version; }
  const CPrivKey& getPrivKey() const { return this->privkey; }
  CPubKey getPubKey() const;

  void setPublicCoin(PublicCoin p) { publicCoin = p; }
  void setRandomness(Bignum n) { randomness = n; }
  void setSerialNumber(Bignum n) { serialNumber = n; }
  void setVersion(uint8_t nVersion) { this->version = nVersion; }
  void setPrivKey(const CPrivKey& privkey) { this->privkey = privkey; }
  bool sign(const uint256& hash, std::vector<unsigned char>& vchSig) const;
  bool IsValid();

  ADD_SERIALIZE_METHODS;
  template <typename Stream, typename Operation>
  inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion) {
    READWRITE(publicCoin);
    READWRITE(randomness);
    READWRITE(serialNumber);

    // NEW
    READWRITE(version);
    READWRITE(privkey);
  }

  static int const PRIVATECOIN_VERSION = 1;

 private:
  const ZerocoinParams* params;
  PublicCoin publicCoin;
  CBigNum randomness;
  CBigNum serialNumber;
  uint8_t version = 1;
  CPrivKey privkey;

  /**
   * @brief Mint a new coin using a faster process.
   * @param denomination the denomination of the coin to mint
   * @throws ZerocoinException if the process takes too long
   *
   * Generates a new Zerocoin by (a) selecting a random serial
   * number, (b) committing to this serial number and repeating until
   * the resulting commitment is prime. Stores the
   * resulting commitment (coin) and randomness (trapdoor).
   * This routine is substantially faster than the
   * mintCoin() routine, but could be more vulnerable
   * to timing attacks. Don't use it if you think someone
   * could be timing your coin minting.
   **/
  void mintCoinFast(const CoinDenomination denomination, CBigNum s, CBigNum r);
};

} /* namespace libzerocoin */