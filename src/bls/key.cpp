// Copyright (c) 2009-2017 The Bitcoin developers
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "key.h"

#include "crypto/common.h"
//#include "crypto/hmac_sha512.h"
#include "random.h"
#include "uint256.h"

namespace ecdsa {

bool CKey::Check(const uint8_t* vch) { return false; }

void CKey::MakeNewKey(bool fCompressedIn) {
  uint8_t keydata[bls::PrivateKey::PRIVATE_KEY_SIZE];
  do { GetRandBytes(keydata, bls::PrivateKey::PRIVATE_KEY_SIZE); } while (!Check(keydata));
  PK = bls::PrivateKey::FromSeed(keydata, bls::PrivateKey::PRIVATE_KEY_SIZE);
}

bool CKey::SetPrivKey(const CPrivKey& privkey, bool fCompressedIn) {
  //  if (!ec_privkey_import_der(secp256k1_context_sign, (uint8_t*)begin(), &privkey[0], privkey.size())) return false;
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
  CPrivKey privkey; // TBD!!!!
  return privkey;
}

CPubKey CKey::GetPubKey() const {
  assert(IsValid());
  //bls::PublicKey tmp_result = PK.GetPublicKey();
  // Wrap to CPubKey....
  CPubKey result;
  return result;
}

bool CKey::Sign(const uint256& hash, std::vector<uint8_t>& vchSig) const {
  /// First Sign,
  bls::Signature sig = PK.Sign(hash.begin(), 32);
  uint8_t sigBytes[bls::Signature::SIGNATURE_SIZE];    // 96 byte array
  sig.Serialize(sigBytes);
  vchSig.resize(bls::Signature::SIGNATURE_SIZE);
  for (size_t i=0;i<bls::Signature::SIGNATURE_SIZE;i++) vchSig[i] = sigBytes[i];
  
  // Then Verify
  return sig.Verify();
}

// Verify that this public key belong to this private Key
bool CKey::VerifyPubKey(const CPubKey& pubkey) const {
  return (pubkey == GetPubKey()); // Check after GetPubKey is fixed
}

bool CKey::Derive(CKey& keyChild, ChainCode& ccChild, unsigned int nChild, const ChainCode& cc) const { return true; }

}
