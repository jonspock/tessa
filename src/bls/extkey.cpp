// Copyright (c) 2009-2017 The Bitcoin developers
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "extkey.h"
#include "pubkey.h"
#include "support/allocators/secure.h"
#include "crypto/common.h"
#include "crypto/hmac_sha512.h"
#include "random.h"
#include "uint256.h"

namespace bls {

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
  key.Set(vout.data(), vout.data() + 32);
  memcpy(chaincode.begin(), vout.data() + 32, 32);

  nDepth = 0;
  nChild = 0;
  memset(vchFingerprint, 0, sizeof(vchFingerprint));
}
void CExtKey::SetMaster(const CKey& k) {
  std::vector<uint8_t> b = k.getBytes();
  static const uint8_t hashkey[] = {'B', 'i', 't', 'c', 'o', 'i', 'n', ' ', 's', 'e', 'e', 'd'};
  std::vector<uint8_t, secure_allocator<uint8_t> > vout(64);
  CHMAC_SHA512(hashkey, sizeof(hashkey)).Write(&b[0], b.size()).Finalize(vout.data());
  key.Set(vout.data(), vout.data() + 32);
  memcpy(chaincode.begin(), vout.data() + 32, 32);
  nDepth = 0;
  nChild = 0;
  memset(vchFingerprint, 0, sizeof(vchFingerprint));
}

void CExtKey::PrintString() const {
  std::cout << "Depth = " << (int)nDepth << " ";
  std::cout << "Child = " << nChild << "\n";
  std::cout << "Fingerprint = "
            << (int)vchFingerprint[0] 
            << (int)vchFingerprint[1] 
            << (int)vchFingerprint[2] 
            << (int)vchFingerprint[3] << "\n";
  std::cout << "Chaincode = " << chaincode.ToString() << "\n";
  std::cout << "key = ";
  key.PrintString();
}

}  // namespace bls
