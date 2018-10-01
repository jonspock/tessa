// Copyright (c) 2014-2017 The Bitcoin Core developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "key_io.h"
#include "base58.h"
#include "bch32/bch32.h"
#include "script/script.h"
#include "utilstrencodings.h"

#include <algorithm>
#include <cassert>
#include <string>
#include <variant>

using namespace ecdsa;
using namespace bch32;

class DestinationEncoder : public std::variant<std::string> {
 private:
  const CChainParams& m_params;

 public:
  DestinationEncoder(const CChainParams& params) : m_params(params) {}

  std::string operator()(const CKeyID& id) const {
    std::vector<uint8_t> data = {0};
    ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, id.begin(), id.end());
    return bch32::encode(m_params.Bch32HRP(), data);
  }

  std::string operator()(const CScriptID& id) const {
    std::vector<uint8_t> data = {0};
    ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, id.begin(), id.end());
    return bch32::encode(m_params.Bch32HRP(), data);
  }

  std::string operator()(const CNoDestination& no) const { return {}; }
};

CTxDestination DecodeDestination(const std::string& str, const CChainParams& params) {
  std::vector<uint8_t> data;
  auto bch = bch32::decode(str);  // pair of string/byte vector
  if (bch.second.size() > 0 && bch.first == params.Bch32HRP()) {
    // Bch32 decoding
    int version = bch.second[0];  // The first 5 bit symbol is the version (0-16)
    // The rest of the symbols are converted witness program bytes.
    data.reserve(((bch.second.size() - 1) * 5) / 8);
    if (ConvertBits<5, 8, false>([&](unsigned char c) { data.push_back(c); }, bch.second.begin() + 1,
                                 bch.second.end())) {
      if (version == 0) {
        {
          CKeyID keyid;
          if (data.size() == keyid.size()) {
            std::copy(data.begin(), data.end(), keyid.begin());
            return keyid;
          }
        }
        {
          CScriptID scriptid;
          if (data.size() == scriptid.size()) {
            std::copy(data.begin(), data.end(), scriptid.begin());
            return scriptid;
          }
        }
        return CNoDestination();
      }
      if (version > 16 || data.size() < 2 || data.size() > 40) { return CNoDestination(); }
    }
  }
  return CNoDestination();
}

// These are just used in Wallet? Can be changed later?

CKey DecodeSecret(const std::string& str) {
  CKey key;
  std::vector<uint8_t> data;
  if (DecodeBase58Check(str, data)) {
    const std::vector<uint8_t>& privkey_prefix = Params().Prefix(CChainParams::SECRET_KEY);
    if ((data.size() == 32 + privkey_prefix.size() ||
         (data.size() == 33 + privkey_prefix.size() && data.back() == 1)) &&
        std::equal(privkey_prefix.begin(), privkey_prefix.end(), data.begin())) {
      bool compressed = data.size() == 33 + privkey_prefix.size();
      key.Set(data.begin() + privkey_prefix.size(), data.begin() + privkey_prefix.size() + 32, compressed);
    }
  }
  memory_cleanse(data.data(), data.size());
  return key;
}

std::string EncodeSecret(const CKey& key) {
  assert(key.IsValid());
  std::vector<uint8_t> data = Params().Prefix(CChainParams::SECRET_KEY);
  data.insert(data.end(), key.begin(), key.end());
  if (key.IsCompressed()) { data.push_back(1); }
  std::string ret = EncodeBase58Check(data);
  memory_cleanse(data.data(), data.size());
  return ret;
}

CExtPubKey DecodeExtPubKey(const std::string& str) {
  CExtPubKey key;
  std::vector<uint8_t> data;
  if (DecodeBase58Check(str, data)) {
    const std::vector<uint8_t>& prefix = Params().Prefix(CChainParams::EXT_PUBLIC_KEY);
    if (data.size() == BIP32_EXTKEY_SIZE + prefix.size() && std::equal(prefix.begin(), prefix.end(), data.begin())) {
      key.Decode(data.data() + prefix.size());
    }
  }
  return key;
}

std::string EncodeExtPubKey(const CExtPubKey& key) {
  std::vector<uint8_t> data = Params().Prefix(CChainParams::EXT_PUBLIC_KEY);
  size_t size = data.size();
  data.resize(size + BIP32_EXTKEY_SIZE);
  key.Encode(data.data() + size);
  std::string ret = EncodeBase58Check(data);
  return ret;
}

CExtKey DecodeExtKey(const std::string& str) {
  CExtKey key;
  std::vector<uint8_t> data;
  if (DecodeBase58Check(str, data)) {
    const std::vector<uint8_t>& prefix = Params().Prefix(CChainParams::EXT_SECRET_KEY);
    if (data.size() == BIP32_EXTKEY_SIZE + prefix.size() && std::equal(prefix.begin(), prefix.end(), data.begin())) {
      key.Decode(data.data() + prefix.size());
    }
  }
  return key;
}

std::string EncodeExtKey(const CExtKey& key) {
  std::vector<uint8_t> data = Params().Prefix(CChainParams::EXT_SECRET_KEY);
  size_t size = data.size();
  data.resize(size + BIP32_EXTKEY_SIZE);
  key.Encode(data.data() + size);
  std::string ret = EncodeBase58Check(data);
  memory_cleanse(data.data(), data.size());
  return ret;
}

std::string EncodeDestination(const CTxDestination& dest) { return std::visit(DestinationEncoder(Params()), dest); }

CTxDestination DecodeDestination(const std::string& str) { return DecodeDestination(str, Params()); }

bool IsValidDestinationString(const std::string& str, const CChainParams& params) {
  return IsValidDestination(DecodeDestination(str, params));
}

bool IsValidDestinationString(const std::string& str) { return IsValidDestinationString(str, Params()); }
