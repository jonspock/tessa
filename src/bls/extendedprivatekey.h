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

#pragma once

#include "privkey.h"
#include "pubkey.h"
#include "chaincode.h"
#include "extendedpublickey.h"

namespace relic {
#include "relic.h"
#include "relic_test.h"
}  // namespace relic

namespace bls12_381 {

/*
Defines a BIP-32 style node, which is composed of a private key and a
chain code. This follows the spec from BIP-0032, with a few changes:
  * The master secret key is generated mod n from the master seed,
    since not all 32 byte sequences are valid BLS private keys
  * Instead of SHA512(input), do SHA256(input || 00000000) ||
    SHA256(input || 00000001)
  * Mod n for the output of key derivation.
  * ID of a key is SHA256(pk) instead of HASH160(pk)
  * Serialization of extended public key is 93 bytes
*/
class ExtendedPrivateKey {
 public:
  // version(4) depth(1) parent fingerprint(4) child#(4) cc(32) sk(32)
  static const uint32_t EXTENDED_PRIVATE_KEY_SIZE = 77;

  // Generates a master private key and chain code from a seed
  static ExtendedPrivateKey FromSeed(const uint8_t* seed, size_t seedLen);

  // Parse private key and chain code from bytes
  static ExtendedPrivateKey FromBytes(const uint8_t* serialized);

  // Derive a child extEnded private key, hardened if i >= 2^31
  ExtendedPrivateKey PrivateChild(uint32_t i) const;

  // Derive a child extended public key, hardened if i >= 2^31
  ExtendedPublicKey PublicChild(uint32_t i) const;

  uint32_t GetVersion() const;
  uint8_t GetDepth() const;
  uint32_t GetParentFingerprint() const;
  uint32_t GetChildNumber() const;

  ChainCode GetChainCode() const;
  CPrivKey GetPrivateKey() const;

  CPubKey GetPublicKey() const;
  ExtendedPublicKey GetExtendedPublicKey() const;

  // Compare to different private key
  friend bool operator==(const ExtendedPrivateKey& a, const ExtendedPrivateKey& b);
  friend bool operator!=(const ExtendedPrivateKey& a, const ExtendedPrivateKey& b);

  void Serialize(uint8_t* buffer) const;

  ~ExtendedPrivateKey();

 private:
  // Private constructor, force use of static methods
  explicit ExtendedPrivateKey(const uint32_t v, const uint8_t d, const uint32_t pfp, const uint32_t cn,
                              const ChainCode code, const CPrivKey key)
      : version(v), depth(d), parentFingerprint(pfp), childNumber(cn), chainCode(code), sk(key) {}

  const uint32_t version;
  const uint8_t depth;
  const uint32_t parentFingerprint;
  const uint32_t childNumber;

  const ChainCode chainCode;
  const CPrivKey sk;
};

}
