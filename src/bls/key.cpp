// Copyright (c) 2009-2017 The Bitcoin developers
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "key.h"

#include "support/allocators/secure.h"
#include "crypto/common.h"
#include "random.h"
#include "uint256.h"

#include <secp256k1.h>
#include <secp256k1_recovery.h>

static secp256k1_context* secp256k1_context_sign = nullptr;

namespace bls {

bool CKey::Check(const uint8_t* vch) { return true; }

void CKey::MakeNewKey() {
  uint8_t keydata[bls::PrivateKey::PRIVATE_KEY_SIZE];
  do { GetRandBytes(keydata, bls::PrivateKey::PRIVATE_KEY_SIZE); } while (!Check(keydata));
  PK = bls::PrivateKey::FromSeed(keydata, bls::PrivateKey::PRIVATE_KEY_SIZE);
}

bool CKey::SetPrivKey(const CPrivKey& privkey) {
  PK = bls::PrivateKey::FromSeed(privkey.begin(), privkey.size());
  return true;
}

uint256 CKey::GetPrivKey_256() {
  uint8_t keydata[bls::PrivateKey::PRIVATE_KEY_SIZE];
  PK.Serialize(keydata);
  uint256* key_256 = (uint256*)keydata;
  return *key_256;
}

CPrivKey CKey::GetPrivKey() const {
  assert(IsValid());
  std::vector<uint8_t> b = getBytes();
  CPrivKey privkey = CPrivKey::FromBytes(&b[0]);
  return privkey;
}

CPubKey CKey::GetPubKey() const {
  bls::PublicKey tmp = PK.GetPublicKey();
  std::vector<uint8_t> b = tmp.Serialize();
  CPubKey result = CPubKey::FromBytes(&b[0]);
  return result;
}

bool CKey::Sign(const uint256& hash, std::vector<uint8_t>& vchSig) const {
  /// First Sign,
  bls::Signature sig = PK.Sign(hash.begin(), 32);
  uint8_t sigBytes[bls::Signature::SIGNATURE_SIZE];  // 96 byte array
  sig.Serialize(sigBytes);
  vchSig.resize(bls::Signature::SIGNATURE_SIZE);
  for (size_t i = 0; i < bls::Signature::SIGNATURE_SIZE; i++) vchSig[i] = sigBytes[i];

  // Then Verify
  return sig.Verify();
}

// Verify that this public key belong to this private Key
bool CKey::VerifyPubKey(const CPubKey& pubkey) const {
  return (pubkey == GetPubKey());  // Check after GetPubKey is fixed
}

bool CKey::Derive(CKey& keyChild, ChainCode& ccChild, unsigned int nChild, const ChainCode& cc) const {
  assert(IsValid());
  std::vector<uint8_t> b = getBytes();
  std::vector<uint8_t, secure_allocator<uint8_t> > vout(64);
  // Assuming PUB KEY is not compressed
  assert(size() == 32);
  BIP32Hash(cc, nChild, 0, &b[0], vout.data());
  memcpy(ccChild.begin(), vout.data() + 32, 32);
  // For now
  bool ret = secp256k1_ec_privkey_tweak_add(secp256k1_context_sign, &b[0], vout.data());
  keyChild.Set(&b[0], &b[32]);
  return ret;
}

}  // namespace bls
