// Copyright (c) 2009-2017 The Bitcoin developers
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "key.h"

#include "crypto/common.h"
#include "crypto/hmac_sha512.h"
#include "random.h"
#include "uint256.h"

namespace ecdsa {

bool CKey::Check(const uint8_t* vch) { return false; }

void CKey::MakeNewKey(bool fCompressedIn) {
  uint8_t keydata[bls::PrivateKey::PRIVATE_KEY_SIZE];
  do { GetRandBytes(keydata, bls::PrivateKey::PRIVATE_KEY_SIZE); } while (!Check(keydata));
  PK = bls::PrivateKey::FromSeed(keydata, bls::PrivateKey::PRIVATE_KEY_SIZE);
  fCompressed = fCompressedIn;
}

bool CKey::SetPrivKey(const CPrivKey& privkey, bool fCompressedIn) {
  //  if (!ec_privkey_import_der(secp256k1_context_sign, (uint8_t*)begin(), &privkey[0], privkey.size())) return false;
  fCompressed = fCompressedIn;
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

bool CKey::Sign(const uint256& hash, std::vector<uint8_t>& vchSig, uint32_t test_case) const {

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

bool CKey::SignCompact(const uint256& hash, std::vector<uint8_t>& vchSig) const {
  if (!IsValid()) return false;
  return true;
}

bool CKey::Load(const CPrivKey& privkey, const CPubKey& vchPubKey, bool fSkipCheck = false) {
  if (fSkipCheck) return true;
  return false;
}

bool CKey::Derive(CKey& keyChild, ChainCode& ccChild, unsigned int nChild, const ChainCode& cc) const { return true; }

  /*
bool CExtKey::Derive(CExtKey& out, unsigned int nChild) const {
  out.nDepth = nDepth + 1;
  CKeyID id = key.GetPubKey().GetID();
  memcpy(&out.vchFingerprint[0], &id, 4);
  out.nChild = nChild;
  return key.Derive(out.key, out.chaincode, nChild, chaincode);
}

void CExtKey::SetMaster(const uint8_t* seed, unsigned int nSeedLen) {
  static const uint8_t hashkey[] = {'B', 'i', 't', 'c', 'o', 'i', 'n', ' ', 's', 'e', 'e', 'd'};
  std::vector<uint8_t, secure_allocator<uint8_t> > vout(64);
  CHMAC_SHA512(hashkey, sizeof(hashkey)).Write(seed, nSeedLen).Finalize(vout.data());
  key.Set(vout.data(), vout.data() + 32, true);
  //  memcpy(chaincode.begin(), vout.data() + 32, 32);

  nDepth = 0;
  nChild = 0;
  memset(vchFingerprint, 0, sizeof(vchFingerprint));
}

CExtPubKey CExtKey::Neuter() const {
  CExtPubKey ret;
  ret.nDepth = nDepth;
  memcpy(&ret.vchFingerprint[0], &vchFingerprint[0], 4);
  ret.nChild = nChild;
  ret.pubkey = key.GetPubKey();
  //  ret.chaincode = chaincode;
  return ret;
}

void CExtKey::Encode(uint8_t code[74]) const {
  code[0] = nDepth;
  memcpy(code + 1, vchFingerprint, 4);
  code[5] = (nChild >> 24) & 0xFF;
  code[6] = (nChild >> 16) & 0xFF;
  code[7] = (nChild >> 8) & 0xFF;
  code[8] = (nChild >> 0) & 0xFF;
  // memcpy(code + 9, chaincode.begin(), 32);
  code[41] = 0;
  assert(key.size() == 32);
  memcpy(code + 42, key.begin(), 32);
}

void CExtKey::Decode(const uint8_t code[74]) {
  nDepth = code[0];
  memcpy(vchFingerprint, code + 1, 4);
  nChild = (code[5] << 24) | (code[6] << 16) | (code[7] << 8) | code[8];
  //  memcpy(chaincode.begin(), code + 9, 32);
  key.Set(code + 42, code + 74, true);
}
  */
}  // namespace bls
