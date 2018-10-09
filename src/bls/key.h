// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "privkey.h"
#include "pubkey.h"
#include "uint256.h"

#include "privatekey.hpp"
#include <vector>

inline void ShowBytes(const std::vector<uint8_t>& b) {
  std::cout << "Bytes = " << std::hex;
  for (size_t i = 0; i < b.size(); i++) {
    std::cout << (int)b[i];  // << " ";
    //    if ((i+1)%16 == 0) std::cout << "\n";
  }
  std::cout << std::dec << "\n";
}

namespace bls {

class CPubKey;

// An encapsulated private key that wraps BLS
class CKey {
 private:
  bls::PrivateKey PK;
  //! Check whether the 32-byte array pointed to be vch is valid keydata.
  bool static Check(const uint8_t* vch);

 public:
  //! Construct an invalid private key.
  CKey() {}

  friend bool operator==(const CKey& a, const CKey& b) { return a.PK == b.PK; }

  //! Initialize using begin and end iterators to byte data.
  template <typename T> void Set(const T pbegin, const T pend) {
    if ((size_t(pend - pbegin) == bls::PrivateKey::PRIVATE_KEY_SIZE) && (Check(&pbegin[0]))) {
      PK = bls::PrivateKey::FromBytes((uint8_t*)&pbegin[0], bls::PrivateKey::PRIVATE_KEY_SIZE);
    }
  }

  //! Simple read-only vector-like interface.
  unsigned int size() const { return (PK.valid() ? bls::PrivateKey::PRIVATE_KEY_SIZE : 0); }

  std::vector<uint8_t> getBytes() const { return PK.Serialize(); }

  void PrintString() const { ::ShowBytes(getBytes()); }

  //! Check whether this private key is valid.
  bool IsValid() const { return PK.valid(); }

  //! Initialize from a CPrivKey
  bool SetPrivKey(const CPrivKey& vchPrivKey);

  //! Generate a new private key using a cryptographic PRNG.
  void MakeNewKey();

  uint256 GetPrivKey_256();
  /**
   * Convert the private key to a CPrivKey
   * This is expensive.
   */
  CPrivKey GetPrivKey() const;

  /**
   * Compute the public key from a private key.
   * This is expensive.
   */
  CPubKey GetPubKey() const;

  bool Sign(const uint256& hash, std::vector<uint8_t>& vchSig) const;

  //! Derive BIP32 child key.
  bool Derive(CKey& keyChild, ChainCode& ccChild, unsigned int nChild, const ChainCode& cc) const;

  /**
   * Verify thoroughly whether a private key and a public key match.
   * This is done using a different mechanism than just regenerating it.
   */
  bool VerifyPubKey(const CPubKey& vchPubKey) const;
};
}  // namespace bls
