// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_COMPRESSOR_H
#define BITCOIN_COMPRESSOR_H

#include "primitives/transaction.h"
#include "script/script.h"
#include "serialize.h"

namespace ecdsa {
class CPubKey;
class CKeyID;
}  // namespace ecdsa

class CScriptID;

/** Compact serializer for scripts.
 *
 *  It detects common cases and encodes them much more efficiently.
 *  3 special cases are defined:
 *  * Pay to pubkey hash (encoded as 21 bytes)
 *  * Pay to script hash (encoded as 21 bytes)
 *  * Pay to pubkey starting with 0x02, 0x03 or 0x04 (encoded as 33 bytes)
 *
 *  Other scripts up to 121 bytes require 1 byte + script length. Above
 *  that, scripts up to 16505 bytes require 2 bytes + script length.
 */
class CScriptCompressor {
 private:
  /**
   * make this static for now (there are only 6 special scripts defined)
   * this can potentially be extended together with a new nVersion for
   * transactions, in which case this value becomes dependent on nVersion
   * and nHeight of the enclosing transaction.
   */
  static const uint32_t nSpecialScripts = 6;

  CScript& script;

 protected:
  /**
   * These check for scripts for which a special case with a shorter encoding is defined.
   * They are implemented separately from the CScript test, as these test for exact byte
   * sequence correspondences, and are more strict. For example, IsToPubKey also verifies
   * whether the public key is valid (as invalid ones cannot be represented in compressed
   * form).
   */
  bool IsToKeyID(ecdsa::CKeyID& hash) const;
  bool IsToScriptID(CScriptID& hash) const;
  bool IsToPubKey(ecdsa::CPubKey& pubkey) const;

  bool Compress(std::vector<uint8_t>& out) const;
  uint32_t GetSpecialSize(uint32_t nSize) const;
  bool Decompress(uint32_t nSize, const std::vector<uint8_t>& out);

 public:
  CScriptCompressor(CScript& scriptIn) : script(scriptIn) {}

  uint32_t GetSerializeSize() const {
    std::vector<uint8_t> compr;
    if (Compress(compr)) return compr.size();
    uint32_t nSize = script.size() + nSpecialScripts;
    return script.size() + VARINT(nSize).GetSerializeSize();
  }

  template <typename Stream> void Serialize(Stream& s) const {
    std::vector<uint8_t> compr;
    if (Compress(compr)) {
      s << CFlatData(compr);
      return;
    }
    uint32_t nSize = script.size() + nSpecialScripts;
    s << VARINT(nSize);
    s << CFlatData(script);
  }

  template <typename Stream> void Unserialize(Stream& s) {
    uint32_t nSize = 0;
    s >> VARINT(nSize);
    if (nSize < nSpecialScripts) {
      std::vector<uint8_t> vch(GetSpecialSize(nSize), 0x00);
      s >> REF(CFlatData(vch));
      Decompress(nSize, vch);
      return;
    }
    nSize -= nSpecialScripts;
    script.resize(nSize);
    s >> REF(CFlatData(script));
  }
};

/** wrapper for CTxOut that provides a more compact serialization */
class CTxOutCompressor {
 private:
  CTxOut& txout;

 public:
  static uint64_t CompressAmount(uint64_t nAmount);
  static uint64_t DecompressAmount(uint64_t nAmount);

  CTxOutCompressor(CTxOut& txoutIn) : txout(txoutIn) {}

  ADD_SERIALIZE_METHODS

  template <typename Stream, typename Operation> inline void SerializationOp(Stream& s, Operation ser_action) {
    if (!ser_action.ForRead()) {
      uint64_t nVal = CompressAmount(txout.nValue);
      READWRITE(VARINT(nVal));
    } else {
      uint64_t nVal = 0;
      READWRITE(VARINT(nVal));
      txout.nValue = DecompressAmount(nVal);
    }
    CScriptCompressor cscript(REF(txout.scriptPubKey));
    READWRITE(cscript);
  }
};

#endif  // BITCOIN_COMPRESSOR_H
