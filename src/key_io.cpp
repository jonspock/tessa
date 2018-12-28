// Copyright (c) 2014-2017 The Bitcoin Core developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "key_io.h"
#include "bch32/bch32.h"
#include "script/script.h"
#include "support/allocators/secure.h"
#include "utilstrencodings.h"

#include <algorithm>
#include <cassert>
#include <string>
#include <variant>

using namespace bls;
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
  auto bch = bch32::decode(str);  // pair of string/byte vector
  if (bch.second.size() > 0 && bch.first == Params().Bch32SEC()) {
    // Bch32 decoding
    int version = bch.second[0];  // The first 5 bit symbol is the version (0-16)
    // The rest of the symbols are converted
    data.reserve(((bch.second.size() - 1) * 5) / 8);
    if (ConvertBits<5, 8, false>([&](unsigned char c) { data.push_back(c); }, bch.second.begin() + 1,
                                 bch.second.end())) {
      key.Set(data.begin(), data.begin() + 32);
    }
  }
  memory_cleanse(data.data(), data.size());
  return key;
}

std::string EncodeSecret(const CKey& key) {
  assert(key.IsValid());
  std::vector<uint8_t> vch = key.getBytes();  // since key doesn't have iterator
  std::vector<uint8_t> data = {0};
  ConvertBits<8, 5, true>([&](unsigned char c) { data.push_back(c); }, vch.begin(), vch.end());
  std::string ret = bch32::encode(Params().Bch32SEC(), data);
  memory_cleanse(data.data(), data.size());
  return ret;
}

std::string EncodeDestination(const CTxDestination& dest) { return std::visit(DestinationEncoder(Params()), dest); }

CTxDestination DecodeDestination(const std::string& str) { return DecodeDestination(str, Params()); }

bool IsValidDestinationString(const std::string& str, const CChainParams& params) {
  return IsValidDestination(DecodeDestination(str, params));
}

bool IsValidDestinationString(const std::string& str) { return IsValidDestinationString(str, Params()); }
