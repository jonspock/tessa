// Copyright (c) 2014 The Bitcoin developers
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "baseBLS.h"

#include "bls/key.h"
#include "bls/privkey.h"
#include "bls/pubkey.h"
#include "hash.h"
#include "support/cleanse.h"
#include "uint256.h"
#include <variant>
#include <cassert>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

using namespace bls;

/** All alphanumeric characters except for "0", "I", "O", and "l" */
static const char* pszBaseBLS = "123456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz";

bool DecodeBaseBLS(const char* psz, std::vector<uint8_t>& vch) {
  // Skip leading spaces.
  while (*psz && isspace(*psz)) psz++;
  // Skip and count leading '1's.
  int zeroes = 0;
  while (*psz == '1') {
    zeroes++;
    psz++;
  }
  // Allocate enough space in big-endian base256 representation.
  std::vector<uint8_t> b256(strlen(psz) * 733 / 1000 + 1);  // log(58) / log(256), rounded up.
  // Process the characters.
  while (*psz && !isspace(*psz)) {
    // Decode base58 character
    const char* ch = strchr(pszBaseBLS, *psz);
    if (ch == nullptr) return false;
    // Apply "b256 = b256 * 58 + ch".
    int carry = ch - pszBaseBLS;
    for (std::vector<uint8_t>::reverse_iterator it = b256.rbegin(); it != b256.rend(); it++) {
      carry += 58 * (*it);
      *it = carry % 256;
      carry /= 256;
    }
    assert(carry == 0);
    psz++;
  }
  // Skip trailing spaces.
  while (isspace(*psz)) psz++;
  if (*psz != 0) return false;
  // Skip leading zeroes in b256.
  std::vector<uint8_t>::iterator it = b256.begin();
  while (it != b256.end() && *it == 0) it++;
  // Copy result into output vector.
  vch.reserve(zeroes + (b256.end() - it));
  vch.assign(zeroes, 0x00);
  while (it != b256.end()) vch.push_back(*(it++));
  return true;
}

std::string DecodeBaseBLS(const char* psz) {
  std::vector<uint8_t> vch;
  DecodeBaseBLS(psz, vch);
  std::stringstream ss;
  ss << std::hex;

  for (unsigned int i = 0; i < vch.size(); i++) {
    uint8_t* c = &vch[i];
    ss << std::setw(2) << std::setfill('0') << (int)c[0];
  }

  return ss.str();
}

std::string EncodeBaseBLS(const uint8_t* pbegin, const uint8_t* pend) {
  // Skip & count leading zeroes.
  int zeroes = 0;
  while (pbegin != pend && *pbegin == 0) {
    pbegin++;
    zeroes++;
  }
  // Allocate enough space in big-endian baseBLS representation.
  std::vector<uint8_t> b58((pend - pbegin) * 138 / 100 + 1);  // log(256) / log(58), rounded up.
  // Process the bytes.
  while (pbegin != pend) {
    int carry = *pbegin;
    // Apply "b58 = b58 * 256 + ch".
    for (std::vector<uint8_t>::reverse_iterator it = b58.rbegin(); it != b58.rend(); it++) {
      carry += 256 * (*it);
      *it = carry % 58;
      carry /= 58;
    }
    assert(carry == 0);
    pbegin++;
  }
  // Skip leading zeroes in base58 result.
  std::vector<uint8_t>::iterator it = b58.begin();
  while (it != b58.end() && *it == 0) it++;
  // Translate the result into a string.
  std::string str;
  str.reserve(zeroes + (b58.end() - it));
  str.assign(zeroes, '1');
  while (it != b58.end()) str += pszBaseBLS[*(it++)];
  return str;
}

std::string EncodeBaseBLS(const std::vector<uint8_t>& vch) { return EncodeBaseBLS(&vch[0], &vch[0] + vch.size()); }

bool DecodeBaseBLS(const std::string& str, std::vector<uint8_t>& vchRet) { return DecodeBaseBLS(str.c_str(), vchRet); }

std::string EncodeBaseBLSCheck(const std::vector<uint8_t>& vchIn) {
  // add 4-byte hash check to the end
  std::vector<uint8_t> vch(vchIn);
  uint256 hash = Hash(vch.begin(), vch.end());
  vch.insert(vch.end(), (uint8_t*)&hash, (uint8_t*)&hash + 4);
  return EncodeBaseBLS(vch);
}

bool DecodeBaseBLSCheck(const char* psz, std::vector<uint8_t>& vchRet) {
  if (!DecodeBaseBLS(psz, vchRet) || (vchRet.size() < 4)) {
    vchRet.clear();
    return false;
  }
  // re-calculate the checksum, insure it matches the included 4-byte checksum
  uint256 hash = Hash(vchRet.begin(), vchRet.end() - 4);
  if (memcmp(&hash, &vchRet.end()[-4], 4) != 0) {
    vchRet.clear();
    return false;
  }
  vchRet.resize(vchRet.size() - 4);
  return true;
}

bool DecodeBaseBLSCheck(const std::string& str, std::vector<uint8_t>& vchRet) {
  return DecodeBaseBLSCheck(str.c_str(), vchRet);
}

CBaseBLSData::CBaseBLSData() {
  vchVersion.clear();
  vchData.clear();
}

void CBaseBLSData::SetData(const std::vector<uint8_t>& vchVersionIn, const void* pdata, size_t nSize) {
  vchVersion = vchVersionIn;
  vchData.resize(nSize);
  if (!vchData.empty()) memcpy(&vchData[0], pdata, nSize);
}

void CBaseBLSData::SetData(const std::vector<uint8_t>& vchVersionIn, const uint8_t* pbegin, const uint8_t* pend) {
  SetData(vchVersionIn, (void*)pbegin, pend - pbegin);
}

bool CBaseBLSData::SetString(const char* psz, unsigned int nVersionBytes) {
  std::vector<uint8_t> vchTemp;
  bool rc58 = DecodeBaseBLSCheck(psz, vchTemp);
  if ((!rc58) || (vchTemp.size() < nVersionBytes)) {
    vchData.clear();
    vchVersion.clear();
    return false;
  }
  vchVersion.assign(vchTemp.begin(), vchTemp.begin() + nVersionBytes);
  vchData.resize(vchTemp.size() - nVersionBytes);
  if (!vchData.empty()) memcpy(&vchData[0], &vchTemp[nVersionBytes], vchData.size());
  memory_cleanse(&vchTemp[0], vchData.size());
  return true;
}

bool CBaseBLSData::SetString(const std::string& str) { return SetString(str.c_str()); }

std::string CBaseBLSData::ToString() const {
  std::vector<uint8_t> vch = vchVersion;
  vch.insert(vch.end(), vchData.begin(), vchData.end());
  return EncodeBaseBLSCheck(vch);
}

int CBaseBLSData::CompareTo(const CBaseBLSData& b58) const {
  if (vchVersion < b58.vchVersion) return -1;
  if (vchVersion > b58.vchVersion) return 1;
  if (vchData < b58.vchData) return -1;
  if (vchData > b58.vchData) return 1;
  return 0;
}

namespace {
class CTessaAddressVisitor : public std::variant<bool> {
 private:
  CTessaAddress* addr;

 public:
  CTessaAddressVisitor(CTessaAddress* addrIn) : addr(addrIn) {}

  bool operator()(const CKeyID& id) const { return addr->Set(id); }
  bool operator()(const CScriptID& id) const { return addr->Set(id); }
  bool operator()(const CNoDestination& no) const { return false; }
};

}  // namespace

bool CTessaAddress::Set(const CKeyID& id) {
  SetData(Params().Prefix(CChainParams::PUBKEY_ADDRESS), &id, 20);
  return true;
}

bool CTessaAddress::Set(const CScriptID& id) {
  SetData(Params().Prefix(CChainParams::SCRIPT_ADDRESS), &id, 20);
  return true;
}

bool CTessaAddress::Set(const CTxDestination& dest) { return std::visit(CTessaAddressVisitor(this), dest); }

bool CTessaAddress::IsValid() const { return IsValid(Params()); }

bool CTessaAddress::IsValid(const CChainParams& params) const {
  bool fCorrectSize = vchData.size() == 20;
  bool fKnownVersion = vchVersion == params.Prefix(CChainParams::PUBKEY_ADDRESS) ||
                       vchVersion == params.Prefix(CChainParams::SCRIPT_ADDRESS);
  return fCorrectSize && fKnownVersion;
}

CTxDestination CTessaAddress::Get() const {
  if (!IsValid()) return CNoDestination();
  uint160 id;
  memcpy(&id, &vchData[0], 20);
  if (vchVersion == Params().Prefix(CChainParams::PUBKEY_ADDRESS))
    return CKeyID(id);
  else if (vchVersion == Params().Prefix(CChainParams::SCRIPT_ADDRESS))
    return CScriptID(id);
  else
    return CNoDestination();
}

bool CTessaAddress::GetKeyID(CKeyID& keyID) const {
  if (!IsValid() || vchVersion != Params().Prefix(CChainParams::PUBKEY_ADDRESS)) return false;
  uint160 id;
  memcpy(&id, &vchData[0], 20);
  keyID = CKeyID(id);
  return true;
}

bool CTessaAddress::IsScript() const {
  return IsValid() && vchVersion == Params().Prefix(CChainParams::SCRIPT_ADDRESS);
}

void CTessaSecret::SetKey(const CKey& vchSecret) {
  assert(vchSecret.IsValid());
  SetData(Params().Prefix(CChainParams::SECRET_KEY), vchSecret.begin(), vchSecret.size());
  if (vchSecret.IsCompressed()) vchData.push_back(1);
}

CKey CTessaSecret::GetKey() {
  CKey ret;
  assert(vchData.size() >= 32);
  ret.Set(vchData.begin(), vchData.begin() + 32, vchData.size() > 32 && vchData[32] == 1);
  return ret;
}

bool CTessaSecret::IsValid() const {
  bool fExpectedFormat = vchData.size() == 32 || (vchData.size() == 33 && vchData[32] == 1);
  bool fCorrectVersion = vchVersion == Params().Prefix(CChainParams::SECRET_KEY);
  return fExpectedFormat && fCorrectVersion;
}

bool CTessaSecret::SetString(const char* pszSecret) { return CBaseBLSData::SetString(pszSecret) && IsValid(); }

bool CTessaSecret::SetString(const std::string& strSecret) { return SetString(strSecret.c_str()); }
