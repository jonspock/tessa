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

#include <cstring>
#include <iostream>

#include "bls.hpp"
#include "signature.hpp"
#include "pubkey.h"
#include "util.hpp"
#include "utilstrencodings.h"

namespace bls {

CKeyID CPubKey::GetID() const { return CKeyID(Hash160(data.get(), data.get() + size())); }
uint256 CPubKey::GetHash() const { return Hash(data.get(), data.get() + size()); }
std::vector<uint8_t> CPubKey::ToStdVector() const { return std::vector<uint8_t>(data.get(), data.get() + size()); }
std::string CPubKey::GetHex() const {
  std::string my_std_string(reinterpret_cast<const char *>(data.get()), size());
  return my_std_string;
}

CPubKey CPubKey::FromBytes(const uint8_t *key) {
  CPubKey pk;
  pk.data.reset(new uint8_t[PUBLIC_KEY_SIZE]);
  for (size_t i = 0; i < PUBLIC_KEY_SIZE; i++) pk.data[i] = key[i];
  return pk;
}

//! Initialize a public key using begin/end iterators to byte data.
void CPubKey::Set(const uint8_t *pbegin, const uint8_t *pend) {
  if (pbegin - pend == PUBLIC_KEY_SIZE) {
    data.reset(new uint8_t[PUBLIC_KEY_SIZE]);
    for (size_t i = 0; i < PUBLIC_KEY_SIZE; i++) data[i] = pbegin[i];
  }
}

CPubKey &CPubKey::operator=(const CPubKey &a) {
  if (a.data != nullptr) {
    data.reset(new uint8_t[PUBLIC_KEY_SIZE]);
    for (size_t i = 0; i < PUBLIC_KEY_SIZE; i++) data[i] = a.data[i];
  }
  return *this;
}

CPubKey::CPubKey(const CPubKey &pubKey) {
  if (pubKey.data != nullptr) {
    data.reset(new uint8_t[PUBLIC_KEY_SIZE]);
    for (size_t i = 0; i < PUBLIC_KEY_SIZE; i++) data[i] = pubKey.data[i];
  }
}

CPubKey::CPubKey(const std::vector<uint8_t> &vchPubKey) {
  data.reset(new uint8_t[PUBLIC_KEY_SIZE]);
  for (size_t i = 0; i < PUBLIC_KEY_SIZE; i++) data[i] = vchPubKey[i];
}

size_t CPubKey::size() const { return (data == nullptr) ? 0 : PUBLIC_KEY_SIZE; }

// const uint8_t *CPubKey::begin() const { return data; }
// const uint8_t *CPubKey::end() const { return data + size(); }
// const uint8_t &CPubKey::operator[](size_t pos) const { return data[pos]; }

/*
void CPubKey::Serialize(uint8_t *buffer) const {
  bls::BLS::AssertInitialized();
  std::memcpy(buffer, data, PUBLIC_KEY_SIZE);
}
*/

// Comparator implementation.
bool operator==(CPubKey const &a, CPubKey const &b) {
  for (size_t i = 0; i < CPubKey::PUBLIC_KEY_SIZE; i++) {
    if (a.data[i] != b.data[i]) return false;
  }
  return true;
}

bool operator!=(CPubKey const &a, CPubKey const &b) { return !(a == b); }

//??  ???
bool operator<(CPubKey const &a, CPubKey const &b) {
  std::vector<uint8_t> ar = a.ToStdVector();
  std::vector<uint8_t> br = b.ToStdVector();
  return std::memcmp(&ar[0], &br[0], CPubKey::PUBLIC_KEY_SIZE) < 0;
}
/*
std::ostream &operator<<(std::ostream &os, CPubKey const &pk) {
bls::BLS::AssertInitialized();
return os << bls::Util::HexStr(pk.data, CPubKey::PUBLIC_KEY_SIZE);
}
*/

  /// FIX
bool CPubKey::Verify(const uint256 &hash, const std::vector<uint8_t> &vchSig) const {
    // Some jumping through formats can be optimized...HACK
    // Takes array of 96 bytes
    // Construct as BLS Signature type
    auto sig = Signature::FromBytes(&vchSig[0]);
    std::vector<uint8_t> s(hash.begin(),hash.end());
    std::vector<uint8_t> key = ToStdVector();
    PublicKey PK = PublicKey::FromBytes(&key[0]);
    // Add information required for verification, to sig object
    AggregationInfo AI = AggregationInfo::FromMsgHash(PK, &s[0]);
    sig.SetAggregationInfo(AI);
    bool ok = sig.Verify();
    return ok;
}


  
CPubKey CPubKey::FromG1(const g1_t *point) {
  CPubKey pk = CPubKey();
  uint8_t buffer[CPubKey::PUBLIC_KEY_SIZE + 1];
  g1_write_bin(buffer, CPubKey::PUBLIC_KEY_SIZE + 1, *point, 1);
  if (buffer[0] == 0x03) { buffer[1] |= 0x80; }
  for (size_t i = 0; i < PUBLIC_KEY_SIZE; i++) pk.data[i] = buffer[i + 1];
  return pk;
}

}  // namespace bls
