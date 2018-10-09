// Copyright 2018 Chia Network Inc

// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//    http://www.apache.org/licenses/LICENSE-2.0

// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <string>

#include "bls.hpp"
#include "privkey.h"
#include "util.hpp"

using namespace bls;
using bls::BLS;

CPrivKey CPrivKey::FromSeed(const uint8_t* seed, size_t seedLen) {
  BLS::AssertInitialized();

  // "BLS private key seed" in ascii
  const uint8_t hmacKey[] = {66,  76, 83,  32,  112, 114, 105, 118, 97,  116,
                             101, 32, 107, 101, 121, 32,  115, 101, 101, 100};

  uint8_t* hash = Util::SecAlloc<uint8_t>(CPrivKey::PRIVATE_KEY_SIZE);

  // Hash the seed into sk
  relic::md_hmac(hash, seed, seedLen, hmacKey, sizeof(hmacKey));

  relic::bn_t order;
  bn_new(order);
  g1_get_ord(order);

  // Make sure private key is less than the curve order
  relic::bn_t* skBn = Util::SecAlloc<relic::bn_t>(1);
  bn_new(*skBn);
  bn_read_bin(*skBn, hash, CPrivKey::PRIVATE_KEY_SIZE);
  bn_mod_basic(*skBn, *skBn, order);

  CPrivKey k = CPrivKey();
  k.AllocateKeyData();
  bn_copy(*k.keydata, *skBn);

  Util::SecFree(skBn);
  Util::SecFree(hash);
  return k;
}

// Construct a private key from a bytearray.
CPrivKey CPrivKey::FromBytes(const uint8_t* bytes) {
  BLS::AssertInitialized();
  CPrivKey k = CPrivKey();
  k.AllocateKeyData();
  bn_read_bin(*k.keydata, bytes, CPrivKey::PRIVATE_KEY_SIZE);
  relic::bn_t ord;
  bn_new(ord);
  g1_get_ord(ord);
  if (bn_cmp(*k.keydata, ord) > 0) { throw std::string("Key data too large, must be smaller than group order"); }
  return k;
}

// Construct a private key from another private key.
CPrivKey::CPrivKey(const CPrivKey& privateKey) {
  BLS::AssertInitialized();
  AllocateKeyData();
  bn_copy(*keydata, *privateKey.GetValue());
}

CPrivKey::~CPrivKey() {
  BLS::AssertInitialized();
  Util::SecFree(keydata);
}

bls::CPubKey CPrivKey::GetPublicKey() const {
  BLS::AssertInitialized();
  relic::g1_t* q = Util::SecAlloc<relic::g1_t>(1);
  g1_mul_gen(*q, *keydata);

  const bls::CPubKey ret = bls::CPubKey::FromG1(q);
  Util::SecFree(*q);
  return ret;
}

bool operator==(const CPrivKey& a, const CPrivKey& b) {
  BLS::AssertInitialized();
  return bn_cmp(*a.keydata, *b.keydata) == CMP_EQ;
}

bool operator!=(const CPrivKey& a, const CPrivKey& b) {
  BLS::AssertInitialized();
  return !(a == b);
}

CPrivKey& CPrivKey::operator=(const CPrivKey& rhs) {
  BLS::AssertInitialized();
  Util::SecFree(keydata);
  AllocateKeyData();
  bn_copy(*keydata, *rhs.GetValue());
  return *this;
}

size_t CPrivKey::size() const {
  BLS::AssertInitialized();
  return CPrivKey::PRIVATE_KEY_SIZE;
}

uint8_t* CPrivKey::begin() const {
  BLS::AssertInitialized();
  return reinterpret_cast<uint8_t*>((*keydata)->dp);
}
uint8_t* CPrivKey::end() const {
  BLS::AssertInitialized();
  return reinterpret_cast<uint8_t*>((*keydata)->dp) + size();
}
/*
void CPrivKey::Serialize(uint8_t* buffer) const {
BLS::AssertInitialized();
bn_write_bin(buffer, CPrivKey::PRIVATE_KEY_SIZE, *keydata);
}
*/

Signature CPrivKey::Sign(uint8_t* msg, size_t len) const {
  BLS::AssertInitialized();
  uint8_t messageHash[BLS::MESSAGE_HASH_LEN];
  Util::Hash256(messageHash, msg, len);
  return SignPrehashed(messageHash);
}

Signature CPrivKey::SignPrehashed(uint8_t* messageHash) const {
  BLS::AssertInitialized();
  relic::g2_t sig, point;

  g2_map(point, messageHash, BLS::MESSAGE_HASH_LEN, 0);
  g2_mul(sig, point, *keydata);

  Signature ret = Signature::FromG2(&sig);

  //  ret.SetAggregationInfo(AggregationInfo::FromMsgHash(GetPublicKey(), messageHash));

  return ret;
}

void CPrivKey::AllocateKeyData() {
  BLS::AssertInitialized();
  keydata = Util::SecAlloc<relic::bn_t>(1);
  bn_new(*keydata);  // Freed in destructor
}
