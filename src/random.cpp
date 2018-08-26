// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2016 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "random.h"
#include "bignum.h"
#include "rand_bignum.h"
#include "support/cleanse.h"
#include <sodium/core.h>
#include <sodium/randombytes.h>
#include <cstdlib>
#include <limits>
#include <thread>

void GetRandBytes(std::vector<uint8_t> &buf) { randombytes_buf(buf.data(), buf.size()); }

void GetRandBytes(uint8_t *out, int num) {
  randombytes_buf(out, num);
}

void GetStrongRandBytes(uint8_t *out, int num) {
  randombytes_buf(out, num);
}

uint64_t GetRand(uint64_t nMax) {
  if (nMax == 0) { return 0; }
  // The range of the random source must be a multiple of the modulus to give
  // every possible output value an equal possibility
  uint64_t nRange = (std::numeric_limits<uint64_t>::max() / nMax) * nMax;
  uint64_t nRand = 0;
  do { GetRandBytes((uint8_t *)&nRand, sizeof(nRand)); } while (nRand >= nRange);
  return (nRand % nMax);
}

int GetRandInt(int nMax) { return GetRand(nMax); }

uint256 GetRandHash() {
  if (sodium_init() < 0) {
    // Currently called by Addrman constructor
    throw std::runtime_error("Libsodium initialization failed.");
  }
  uint256 hash;
  GetRandBytes((uint8_t *)&hash, sizeof(hash));
  return hash;
}

void FastRandomContext::RandomSeed() {
  uint256 seed = GetRandHash();
  rng.SetKey(seed.begin(), 32);
  requires_seed = false;
}

uint256 FastRandomContext::rand256() {
  if (bytebuf_size < 32) { FillByteBuffer(); }
  uint256 ret;
  memcpy(ret.begin(), bytebuf + 64 - bytebuf_size, 32);
  bytebuf_size -= 32;
  return ret;
}

std::vector<uint8_t> FastRandomContext::randbytes(size_t len) {
  std::vector<uint8_t> ret(len);
  if (len > 0) { rng.Output(&ret[0], len); }
  return ret;
}

FastRandomContext::FastRandomContext(const uint256 &seed) : requires_seed(false), bytebuf_size(0), bitbuf_size(0) {
  rng.SetKey(seed.begin(), 32);
}

FastRandomContext::FastRandomContext(bool fDeterministic)
    : requires_seed(!fDeterministic), bytebuf_size(0), bitbuf_size(0) {
  if (!fDeterministic) { return; }
  uint256 seed;
  rng.SetKey(seed.begin(), 32);
}

/** Generates a cryptographically secure random number between zero and range exclusive
 * i.e. 0 < returned number < range
 * @param range The upper bound on the number.
 * @return
 */
CBigNum randBignum(const CBigNum &range) {
  size_t size = (mpz_sizeinbase(range.bn, 2) + CHAR_BIT - 1) / CHAR_BIT;
  std::vector<unsigned char> buf(size);

  randombytes_buf(&buf, size);
  CBigNum ret(buf);
  if (ret < 0) mpz_neg(ret.bn, ret.bn);
  return ret;
}

/** Generates a cryptographically secure random k-bit number
 * @param k The bit length of the number.
 * @return
 */
CBigNum RandKBitBigum(const uint32_t k) {
  std::vector<unsigned char> buf((k + 7) / 8);

  randombytes_buf(&buf, (k + 7) / 8);
  CBigNum ret(buf);
  if (ret < 0) mpz_neg(ret.bn, ret.bn);
  return ret;
}

/**
 * Generates a random (safe) prime of numBits bits
 * @param numBits the number of bits
 * @param safe true for a safe prime
 * @return the prime
 */
CBigNum generatePrime(const unsigned int numBits, bool safe) {
  CBigNum rand = RandKBitBigum(numBits);
  CBigNum prime;
  mpz_nextprime(prime.bn, rand.bn);
  return prime;
}
