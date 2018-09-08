// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Why base-BLS instead of standard base-64 encoding?
 * - Don't want 0OIl characters that look the same in some fonts and
 *      could be used to create visually identical looking account numbers.
 * - A string with non-alphanumeric characters is not as easily accepted as an account number.
 * - E-mail usually won't line-break if there's no punctuation to break at.
 * - Double-clicking selects the whole number as one word if it's all alphanumeric.
 */
#pragma once

#include "bls/key.h"
#include "bls/privkey.h"
#include "bls/pubkey.h"

#include "chainparams.h"
#include "script/script.h"
#include "script/standard.h"
#include "support/allocators/zeroafterfree.h"

#include <string>
#include <vector>

/**
 * Encode a byte sequence as a baseBLS-encoded string.
 * pbegin and pend cannot be nullptr, unless both are.
 */
std::string EncodeBaseBLS(const uint8_t* pbegin, const uint8_t* pend);

/**
 * Encode a byte vector as a baseBLS-encoded string
 */
std::string EncodeBaseBLS(const std::vector<uint8_t>& vch);

/**
 * Decode a baseBLS-encoded string (psz) into a byte vector (vchRet).
 * return true if decoding is successful.
 * psz cannot be nullptr.
 */
bool DecodeBaseBLS(const char* psz, std::vector<uint8_t>& vchRet);

/**
 * Decode a baseBLS-encoded string (psz) into a string.
 * psz cannot be nullptr.
 */
std::string DecodeBaseBLS(const char* psz);

/**
 * Decode a baseBLS-encoded string (str) into a byte vector (vchRet).
 * return true if decoding is successful.
 */
bool DecodeBaseBLS(const std::string& str, std::vector<uint8_t>& vchRet);

/**
 * Encode a byte vector into a baseBLS-encoded string, including checksum
 */
std::string EncodeBaseBLSCheck(const std::vector<uint8_t>& vchIn);

/**
 * Decode a baseBLS-encoded string (psz) that includes a checksum into a byte
 * vector (vchRet), return true if decoding is successful
 */
inline bool DecodeBaseBLSCheck(const char* psz, std::vector<uint8_t>& vchRet);

/**
 * Decode a baseBLS-encoded string (str) that includes a checksum into a byte
 * vector (vchRet), return true if decoding is successful
 */
inline bool DecodeBaseBLSCheck(const std::string& str, std::vector<uint8_t>& vchRet);

/**
 * Base class for all baseBLS-encoded data
 */
class CBaseBLSData {
 protected:
  //! the version byte(s)
  std::vector<uint8_t> vchVersion;

  //! the actually encoded data
  typedef std::vector<uint8_t, zero_after_free_allocator<uint8_t> > vector_uchar;
  vector_uchar vchData;

  CBaseBLSData();
  void SetData(const std::vector<uint8_t>& vchVersionIn, const void* pdata, size_t nSize);
  void SetData(const std::vector<uint8_t>& vchVersionIn, const uint8_t* pbegin, const uint8_t* pend);

 public:
  bool SetString(const char* psz, unsigned int nVersionBytes = 1);
  bool SetString(const std::string& str);
  std::string ToString() const;
  int CompareTo(const CBaseBLSData& bBLS) const;

  bool operator==(const CBaseBLSData& bBLS) const { return CompareTo(bBLS) == 0; }
  bool operator<=(const CBaseBLSData& bBLS) const { return CompareTo(bBLS) <= 0; }
  bool operator>=(const CBaseBLSData& bBLS) const { return CompareTo(bBLS) >= 0; }
  bool operator<(const CBaseBLSData& bBLS) const { return CompareTo(bBLS) < 0; }
  bool operator>(const CBaseBLSData& bBLS) const { return CompareTo(bBLS) > 0; }
};

/** baseBLS-encoded Tessa addresses.
 * Public-key-hash-addresses have version 0 (or 111 testnet).
 * The data vector contains RIPEMD160(SHA256(pubkey)), where pubkey is the serialized public key.
 * Script-hash-addresses have version 5 (or 196 testnet).
 * The data vector contains RIPEMD160(SHA256(cscript)), where cscript is the serialized redemption script.
 */
class CTessaAddress : public CBaseBLSData {
 public:
  bool Set(const bls12_381::CKeyID& id);
  bool Set(const CScriptID& id);
  bool Set(const CTxDestination& dest);
  bool IsValid() const;
  bool IsValid(const CChainParams& params) const;

  CTessaAddress() {}
  CTessaAddress(const CTxDestination& dest) { Set(dest); }
  CTessaAddress(const std::string& strAddress) { SetString(strAddress); }
  CTessaAddress(const char* pszAddress) { SetString(pszAddress); }

  CTxDestination Get() const;
  bool GetKeyID(bls12_381::CKeyID& keyID) const;
  bool IsScript() const;
};

/**
 * A baseBLS-encoded secret key
 */

class CTessaSecret : public CBaseBLSData {
 public:
  void SetKey(const bls12_381::CKey& vchSecret);
  bls12_381::CKey GetKey();
  bool IsValid() const;
  bool SetString(const char* pszSecret);
  bool SetString(const std::string& strSecret);

  CTessaSecret(const bls12_381::CKey& vchSecret) { SetKey(vchSecret); }
  CTessaSecret() {}
};

template <typename K, int Size, CChainParams::Base58Type Type> class CTessaExtKeyBase : public CBaseBLSData {
 public:
  void SetKey(const K& key) {
    uint8_t vch[Size];
    key.Encode(vch);
    SetData(Params().Base58Prefix(Type), vch, vch + Size);
  }

  K GetKey() {
    K ret;
    ret.Decode(&vchData[0], &vchData[Size]);
    return ret;
  }

  CTessaExtKeyBase(const K& key) { SetKey(key); }

  CTessaExtKeyBase() {}
};

typedef CTessaExtKeyBase<bls12_381::CExtKey, 74, CChainParams::EXT_SECRET_KEY> CTessaExtKey;
typedef CTessaExtKeyBase<bls12_381::CExtPubKey, 74, CChainParams::EXT_PUBLIC_KEY> CTessaExtPubKey;
