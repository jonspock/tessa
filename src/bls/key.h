// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "privkey.h"
#include "pubkey.h"
#include "serialize.h"
#include "support/allocators/secure.h"
#include "uint256.h"

// from BLS
#include "privatekey.hpp"

#include <vector>

namespace ecdsa {

class CPubKey;

struct CExtPubKey;

// An encapsulated private key that wraps BLS 
class CKey {

 private:
  bls::PrivateKey PK;
  
  //! Check whether the 32-byte array pointed to be vch is valid keydata.
  bool static Check(const uint8_t* vch);

 public:
  //! Construct an invalid private key.
  CKey() {}

  friend bool operator==(const CKey& a, const CKey& b) {
    return a.PK == b.PK;
  }

  //! Initialize using begin and end iterators to byte data.
  template <typename T> void Set(const T pbegin, const T pend) {
    if ((size_t(pend - pbegin) == bls::PrivateKey::PRIVATE_KEY_SIZE) && (Check(&pbegin[0]))) {
      PK.FromBytes((uint8_t*)&pbegin[0], bls::PrivateKey::PRIVATE_KEY_SIZE);
    }
  }

  //! Simple read-only vector-like interface.
  unsigned int size() const { return (PK.valid() ? bls::PrivateKey::PRIVATE_KEY_SIZE : 0); }
//const uint8_t* begin() const { return keydata.data(); }
//  const uint8_t* end() const { return keydata.data() + size(); }

  //! Check whether this private key is valid.
  bool IsValid() const { return PK.valid(); }

  //! Check whether the public key corresponding to this private key is (to be) compressed.
  // Legacy interface
  bool IsCompressed() const { return true; }

  //! Initialize from a CPrivKey
  bool SetPrivKey(const CPrivKey& vchPrivKey, bool fCompressed);

  //! Generate a new private key using a cryptographic PRNG.
  void MakeNewKey(bool fCompressed);

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
