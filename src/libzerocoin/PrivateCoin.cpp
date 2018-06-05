/**
 * @file       PrivateCoin.cpp
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
#include "PrivateCoin.h"
#include "Commitment.h"
#include "Commit.h"
#include "Denominations.h"
#include "IntegerMod.h"
#include "ModulusType.h"
#include "pubkey.h"
#include <stdexcept>

namespace libzerocoin {

bool IsValidCoinValue(const ZerocoinParams* params,
                      const IntegerMod<COIN_COMMITMENT_MODULUS>& C) {
  return (C >= params->accumulatorParams.minCoinValue) &&
    (C <= params->accumulatorParams.maxCoinValue) && C.isPrime(ZEROCOIN_MINT_PRIME_PARAM);
}
  
bool GenerateKeyPair(const CBigNum& bnGroupOrder, const uint256& nPrivkey, CKey& key, CBigNum& bnSerial) {
  // Generate a new key pair, which also has a 256-bit pubkey hash that qualifies as a serial #
  // This builds off of Tim Ruffing's work on libzerocoin, but has a different implementation
  CKey keyPair;
  if (nPrivkey == 0)
    keyPair.MakeNewKey(true);
  else
    keyPair.Set(nPrivkey.begin(), nPrivkey.end(), true);

  CPubKey pubKey = keyPair.GetPubKey();
  uint256 hashPubKey = Hash(pubKey.begin(), pubKey.end());

  CBigNum s(hashPubKey);
  key = keyPair;
  bnSerial = s;
  return true;
}

PrivateCoin::PrivateCoin(const ZerocoinParams* p) : params(p), publicCoin(p) {
  // Verify that the parameters are valid
  if (this->params->initialized == false) { throw std::runtime_error("Params are not initialized"); }
}

PrivateCoin::PrivateCoin(const ZerocoinParams* p, const CoinDenomination denomination, const CBigNum Serial,
                         const CBigNum Randomness)
    : params(p), publicCoin(p) {
  // Verify that the parameters are valid
  if (this->params->initialized == false) { throw std::runtime_error("Params are not initialized"); }

  // Save Parameters & do Commitment
  serialNumber = Serial;
  randomness = Randomness;

  // 2 TEMPLATE PARAMS are the same
  Commitment c = commit<COIN_COMMITMENT_MODULUS, COIN_COMMITMENT_MODULUS>(
      p->coinCommitmentGroup.g, p->coinCommitmentGroup.h, serialNumber, randomness);
  this->publicCoin = PublicCoin(c.getCommitmentValue(), denomination);
}

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

CBigNum PrivateCoin::CoinFromSeed(const uint512& seedZerocoin) {
  CBigNum bnRandomness;
  CBigNum bnSerial;
  CoinDenomination denomination = CoinDenomination::ZQ_ONE; // Not used
  
  // convert state seed into a seed for the private key
  uint256 nSeedPrivKey = seedZerocoin.trim256();
  const IntegerMod<COIN_COMMITMENT_MODULUS> g(this->params->coinCommitmentGroup.g);
  const IntegerMod<COIN_COMMITMENT_MODULUS> h(this->params->coinCommitmentGroup.h);

  bool isValidKey = false;
  CKey key = CKey();
  while (!isValidKey) {
    nSeedPrivKey = Hash(nSeedPrivKey.begin(), nSeedPrivKey.end());
    isValidKey = GenerateKeyPair(params->coinCommitmentGroup.groupOrder, nSeedPrivKey, key, bnSerial);
    setPrivKey(key.GetPrivKey());
  }

  // hash randomness seed with Bottom 256 bits of seedZerocoin & attempts256 which is initially 0
  uint256 randomnessSeed = uint512(seedZerocoin >> 256).trim256();
  uint256 hashRandomness = Hash(randomnessSeed.begin(), randomnessSeed.end());
  bnRandomness.setuint256(hashRandomness);
  bnRandomness = bnRandomness % params->coinCommitmentGroup.groupOrder;

  CBigNum r = bnRandomness;
  CBigNum s = bnSerial;

  /// Manually compute a Pedersen commitment to the serial number "s" under randomness "r"
  /// \f$ C = g^s * h^r (mod p) \f$
  IntegerMod<COIN_COMMITMENT_MODULUS> C = (g ^ s) * (h ^ r);

  CBigNum random;
  uint256 attempts256 = 0;
  // Iterate on Randomness until a valid commitmentValue is found
  while (true) {
    // Now verify that the commitment is a prime number
    // in the appropriate range. If not, we'll throw this coin
    // away and generate a new one.
    if (IsValidCoinValue(params, C.getValue())) {
      serialNumber = s;
      randomness = r;
      publicCoin = PublicCoin(C.getValue(), denomination);
      return C.getValue();
    }

    // Did not create a valid commitment value.
    /// The commitment was not prime. Increment "r" and recalculate "C":
    attempts256++;
    hashRandomness = Hash(randomnessSeed.begin(), randomnessSeed.end(), attempts256.begin(), attempts256.end());
    random.setuint256(hashRandomness);
    /// \f$ r = r + r_delta mod q \f$
    /// \f$ C = C * h mod p \f$
    r = (r + random) % params->coinCommitmentGroup.groupOrder;
    C *= (h ^ random);
  }
}


  
void PrivateCoin::mintCoinFast(const CoinDenomination denomination, CBigNum s, CBigNum r) {
  // Generate a random serial number in the range 0...{q-1} where
  // "q" is the order of the commitment group.
  const IntegerMod<COIN_COMMITMENT_MODULUS> g(this->params->coinCommitmentGroup.g);
  const IntegerMod<COIN_COMMITMENT_MODULUS> h(this->params->coinCommitmentGroup.h);

  // const CBigNum& groupOrder = this->params->coinCommitmentGroup.groupOrder;
  // CBigNum s = CBigNum::randBignum(groupOrder);
  /// Generate a random number "r" in the range 0...{q-1}
  // CBigNum r = CBigNum::randBignum(groupOrder);

  /// Manually compute a Pedersen commitment to the serial number "s" under randomness "r"
  /// \f$ C = g^s * h^r (mod p) \f$
  IntegerMod<COIN_COMMITMENT_MODULUS> C = (g ^ s) * (h ^ r);

  /// Repeat this process up to MAX_COINMINT_ATTEMPTS times until obtain a prime number
  for (uint32_t attempt = 0; attempt < MAX_COINMINT_ATTEMPTS; attempt++) {
    // First verify that the commitment is a prime number
    // in the appropriate range. If not, we'll throw this coin
    // away and generate a new one.
    if (C.isPrime(ZEROCOIN_MINT_PRIME_PARAM) && C >= params->accumulatorParams.minCoinValue &&
        C <= params->accumulatorParams.maxCoinValue) {
      // Found a valid coin. Store it.
      this->serialNumber = s;
      this->randomness = r;
      this->publicCoin = PublicCoin(C.getValue(), denomination);

      // Success! We're done.
      return;
    }

    // Generate a new random "r_delta" in 0...{q-1}
    CBigNum r_delta = CBigNum::randBignum(this->params->coinCommitmentGroup.groupOrder);

    /// The commitment was not prime. Increment "r" and recalculate "C":
    /// \f$ r = r + r_delta mod q \f$
    /// \f$ C = C * h mod p \f$
    r = (r + r_delta) % this->params->coinCommitmentGroup.groupOrder;
    C *= (h ^ r_delta);
  }

  // We only get here if we did not find a coin within
  // MAX_COINMINT_ATTEMPTS. Throw an exception.
  throw std::runtime_error("Unable to mint a new Zerocoin (too many attempts)");
}

CPubKey PrivateCoin::getPubKey() const {
  CKey key;
  key.SetPrivKey(privkey, true);
  return key.GetPubKey();
}

bool PrivateCoin::sign(const uint256& hash, vector<unsigned char>& vchSig) const {
  CKey key;
  key.SetPrivKey(privkey, true);
  return key.Sign(hash, vchSig);
}

} /* namespace libzerocoin */