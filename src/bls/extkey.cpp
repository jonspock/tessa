// Copyright (c) 2009-2017 The Bitcoin developers
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "extkey.h"

#include "crypto/common.h"
#include "crypto/hmac_sha512.h"
#include "random.h"
#include "uint256.h"

namespace ecdsa {

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

}  // namespace bls
