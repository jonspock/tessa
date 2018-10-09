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

#include "hash.h"
#include "uint256.h"
#include "util.hpp"
#include <vector>

namespace bls {

static const size_t BIP32_EXTKEY_SIZE = 74;  // TBD

// A reference to a CKey: the Hash160 of its serialized public key
class CKeyID : public uint160 {
 public:
  CKeyID() : uint160() {}
  explicit CKeyID(const uint160 &in) : uint160(in) {}
};

/** An encapsulated public key. */
class CPubKey {
 public:
  static const size_t PUBLIC_KEY_SIZE = 48;
  static const size_t SIGNATURE_SIZE = 96;

  // Construct a public key from a byte vector.
  static CPubKey FromBytes(const uint8_t *key);

  // Construct a public key from another public key.
  CPubKey(const CPubKey &pubKey);
  CPubKey(const std::vector<uint8_t> &vchPubKey);
  CPubKey &operator=(const CPubKey &a);
  CPubKey() = default;  //  std::cout << "Init of CPubKey\n";

  void PrintString() const {
    if (data == nullptr) {
      std::cout << "nullptr";
    } else {
      for (size_t i = 0; i < PUBLIC_KEY_SIZE; i++) std::cout << std::hex << (int)data[i];
      std::cout << "\n";
    }
  }

  size_t size() const;
  //! Initialize a public key using begin/end iterators to byte data.
  void Set(const uint8_t *pbegin, const uint8_t *pend);

  // Comparator implementation.
  friend bool operator==(CPubKey const &a, CPubKey const &b);
  friend bool operator!=(CPubKey const &a, CPubKey const &b);
  friend bool operator<(CPubKey const &a, CPubKey const &b);
  friend std::ostream &operator<<(std::ostream &os, CPubKey const &s);

  std::vector<uint8_t> ToStdVector() const;
  std::string GetHex() const;

  // Verify a signature
  bool Verify(const uint256 &hash, const std::vector<uint8_t> &vchSig) const;
  bool IsValid() const { return (data != nullptr); }
  bool IsFullyValid() const { return IsValid(); }  // not sure why different HACK

  uint256 GetHash() const;
  CKeyID GetID() const;
  static CPubKey FromG1(const relic::g1_t *key);

  //! Implement serialization, as if this was a byte vector.
  unsigned int GetSerializeSize() const { return size(); }
  template <typename Stream> void Serialize(Stream &s) const {
    ::WriteCompactSize(s, size());
    s.write((const char *)data.get(), PUBLIC_KEY_SIZE);
  }
  template <typename Stream> void Unserialize(Stream &s) {
    if (data == nullptr) data.reset(new uint8_t[PUBLIC_KEY_SIZE]);
    unsigned int len = ::ReadCompactSize(s);
    if (len <= PUBLIC_KEY_SIZE) { s.read((char *)data.get(), PUBLIC_KEY_SIZE); }
  }

  // private:

  static void CompressPoint(uint8_t *result, const relic::g1_t *point);

  std::unique_ptr<uint8_t[]> data;
};

}  // namespace bls
