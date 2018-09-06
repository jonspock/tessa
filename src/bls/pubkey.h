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

#include <vector>
#include "hash.h"
#include "blsutil.h"
#include "uint256.h"
#include "chaincode.h"

namespace bls12_381 {

  static const size_t BIP32_EXTKEY_SIZE = 1; // TBD

  
// A reference to a CKey: the Hash160 of its serialized public key
class CKeyID : public uint160 {
 public:
  CKeyID() : uint160() {}
  explicit CKeyID(const uint160& in) : uint160(in) {}
};

/** An encapsulated public key. */
class CPubKey {
 public:
  static const size_t PUBLIC_KEY_SIZE = 48;

  // Construct a public key from a byte vector.
  static CPubKey FromBytes(const uint8_t *key);

  // Construct a public key from a native g1 element.
  static CPubKey FromG1(const relic::g1_t *key);

  // Construct a public key from another public key.
  CPubKey(const CPubKey &pubKey);

  // Simple read-only vector-like interface to the pubkey data.
  size_t size() const;
  const uint8_t *begin() const;
  const uint8_t *end() const;
  const uint8_t &operator[](size_t pos) const;

  // Comparator implementation.
  friend bool operator==(CPubKey const &a, CPubKey const &b);
  friend bool operator!=(CPubKey const &a, CPubKey const &b);
  friend bool operator<(CPubKey const &a, CPubKey const &b);
  friend std::ostream &operator<<(std::ostream &os, CPubKey const &s);

  //void Serialize(uint8_t *buffer) const;
  void GetPoint(relic::g1_t &output) const { *output = *q; }

  // Returns the first 4 bytes of the serialized pk
  uint32_t GetFingerprint() const;

  std::vector<uint8_t> Raw() const;

  static bool CheckLowS(const std::vector<uint8_t>& vchSig);

  // Verify a signature 
  bool Verify(const uint256& hash, const std::vector<uint8_t>& vchSig) const { return false; } // for now
  
  // FOR NOW: Don't allow public construction, force static methods
  CPubKey() {}
  CPubKey(const std::vector<uint8_t>& vchPubKey) {}
  template <typename T> CPubKey(const T pbegin, const T pend) { ; }
  
  bool IsValid() const { return size() > 0; }
  bool IsFullyValid() const;
  bool IsCompressed() const; // { return size() == COMPRESSED_PUBLIC_KEY_SIZE; }
  uint256 GetHash() const;// { return Hash(vch, vch + size()); }
  bool RecoverCompact(const uint256& hash, const std::vector<uint8_t>& vchSig) { return false; } // for now
  bool Decompress();

  CKeyID GetID() const { return CKeyID(Hash160(data, data + size())); }

    //! Initialize a public key using begin/end iterators to byte data.
  template <typename T> void Set(const T pbegin, const T pend) {
    /*
    int len = pend == pbegin ? 0 : GetLen(pbegin[0]);
    if (len && len == (pend - pbegin))
      memcpy(vch, (uint8_t*)&pbegin[0], len);
    else
      Invalidate();
    */
  }

  //! Implement serialization, as if this was a byte vector.
  unsigned int GetSerializeSize() const { return size() + 1; }
  template <typename Stream> void Serialize(Stream& s) const {
    /*
    unsigned int len = size();
    //::WriteCompactSize(s, len);
    // TBD s.write((char*)vch, len);
    */
  }
  template <typename Stream> void Unserialize(Stream& s) {
    /*
    unsigned int len = 0; //::ReadCompactSize(s);
    if (len <= PUBLIC_KEY_SIZE) {
      // TBD s.read((char*)vch, len);
    } else {
      // invalid pubkey, skip available data
      //char dummy;
      //while (len--) s.read(&dummy, 1);
      //Invalidate();
    }
    */
  }


  //private:

  static void CompressPoint(uint8_t *result, const relic::g1_t *point);

  // Public key group element
  relic::g1_t q;
  uint8_t data[CPubKey::PUBLIC_KEY_SIZE];
};

  
struct CExtPubKey {
  uint8_t nDepth;
  uint8_t vchFingerprint[4];
  unsigned int nChild;
  ChainCode chaincode;
  CPubKey pubkey;

  friend bool operator==(const CExtPubKey& a, const CExtPubKey& b) {
    return a.nDepth == b.nDepth && memcmp(&a.vchFingerprint[0], &b.vchFingerprint[0], sizeof(vchFingerprint)) == 0 &&
           a.nChild == b.nChild && a.chaincode == b.chaincode && a.pubkey == b.pubkey;
  }

  void Encode(uint8_t code[BIP32_EXTKEY_SIZE]) const;
  void Decode(const uint8_t code[BIP32_EXTKEY_SIZE]);
  bool Derive(CExtPubKey& out, unsigned int nChild) const;

  /*
  void Serialize(CSizeComputer& s) const {
    // Optimized implementation for ::GetSerializeSize that avoids copying.
    s.seek(BIP32_EXTKEY_SIZE + 1);  // add one byte for the size (compact int)
  }
  */
  template <typename Stream> void Serialize(Stream& s) const {
    unsigned int len = BIP32_EXTKEY_SIZE;
    //    ::WriteCompactSize(s, len);
    uint8_t code[BIP32_EXTKEY_SIZE];
    Encode(code);
    s.write((const char*)&code[0], len);
  }
  template <typename Stream> void Unserialize(Stream& s) {
    unsigned int len = 0; //::ReadCompactSize(s);
    uint8_t code[BIP32_EXTKEY_SIZE];
    if (len != BIP32_EXTKEY_SIZE) throw std::runtime_error("Invalid extended key size\n");
    s.read((char*)&code[0], len);
    Decode(code);
  }
};

}
