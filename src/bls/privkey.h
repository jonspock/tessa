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

#include "pubkey.h"
#include "signature.hpp"

class CPrivKey {
 public:
  // Private keys are represented as 32 byte field elements. Note that
  // not all 32 byte integers are valid keys, the private key must be
  // less than the group order (which is in bls.h).
  static const size_t PRIVATE_KEY_SIZE = 32;

  // Generates a private key from a seed, similar to HD key generation
  // (hashes the seed), and reduces it mod the group order.
  static CPrivKey FromSeed(const uint8_t* seed, size_t seedLen);

  // Construct a private key from a bytearray.
  static CPrivKey FromBytes(const uint8_t* bytes);

  // Construct a private key from another private key. Allocates memory in
  // secure heap, and copies keydata.
  CPrivKey(const CPrivKey& k);

  ~CPrivKey();

  bls::CPubKey GetPublicKey() const;

  // Compare to different private key
  friend bool operator==(const CPrivKey& a, const CPrivKey& b);
  friend bool operator!=(const CPrivKey& a, const CPrivKey& b);
  CPrivKey& operator=(const CPrivKey& rhs);

  // Simple read-only vector-like interface.
  size_t size() const;
  uint8_t* begin() const;
  uint8_t* end() const;

  bn_t* GetValue() const { return keydata; }

  // Serialize the key into bytes
  // void Serialize(uint8_t* buffer) const;

  //! Implement serialization, as if this was a byte vector.
  unsigned int GetSerializeSize() const { return size() + 1; }
  template <typename Stream> void Serialize(Stream& s) const {
    /*
    unsigned int len = size();
    ::WriteCompactSize(s, len);
    // TBD s.write((char*)vch, len);
    */
  }
  template <typename Stream> void Unserialize(Stream& s) {
    /*
    unsigned int len = ::ReadCompactSize(s);
    if (len <= PRIVATE_KEY_SIZE) {
      // TBD s.read((char*)vch, len);
    } else {
      // invalid pubkey, skip available data
      char dummy;
      //while (len--) s.read(&dummy, 1);
      //Invalidate();
    }
    */
  }

  // Sign a message
  bls::Signature Sign(uint8_t* msg, size_t len) const;
  bls::Signature SignPrehashed(uint8_t* hash) const;

  // FOR NOW : ALLOW : Don't allow public construction, force static methods
  CPrivKey() {}

  void clear() {
    // BLS::AssertInitialized();
    bls::Util::SecFree(keydata);
  }

  // private:

  // The actual byte data
  bn_t* keydata;

  // Allocate memory for private key
  void AllocateKeyData();
};
