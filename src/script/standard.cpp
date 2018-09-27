// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "script/standard.h"
#include "ecdsa/pubkey.h"
#include "hash.h"
#include "mpark/variant.hpp"
#include "script/script.h"
#include "util.h"
#include "utilstrencodings.h"

using namespace std;
using namespace ecdsa;

typedef vector<uint8_t> valtype;

unsigned nMaxDatacarrierBytes = MAX_OP_RETURN_RELAY;

CScriptID::CScriptID(const CScript& in) : uint160(Hash160(in.begin(), in.end())) {}

const char* GetTxnOutputType(txnouttype t) {
  switch (t) {
    case TX_NONSTANDARD:
      return "nonstandard";
    case TX_PUBKEY:
      return "pubkey";
    case TX_PUBKEYHASH:
      return "pubkeyhash";
    case TX_SCRIPTHASH:
      return "scripthash";
    case TX_MULTISIG:
      return "multisig";
    case TX_NULL_DATA:
      return "nulldata";
    case TX_ZEROCOINMINT:
      return "zerocoinmint";
  }
  return nullptr;
}

/**
 * Return public keys or hashes from scriptPubKey, for 'standard' transaction types.
 */
bool Solver(const CScript& scriptPubKey, txnouttype& typeRet, vector<vector<uint8_t> >& vSolutionsRet) {
  // Templates
  static multimap<txnouttype, CScript> mTemplates;
  if (mTemplates.empty()) {
    // Standard tx, sender provides pubkey, receiver adds signature
    mTemplates.insert(make_pair(TX_PUBKEY, CScript() << OP_PUBKEY << OP_CHECKSIG));

    // Bitcoin address tx, sender provides hash of pubkey, receiver provides signature and pubkey
    mTemplates.insert(
        make_pair(TX_PUBKEYHASH, CScript() << OP_DUP << OP_HASH160 << OP_PUBKEYHASH << OP_EQUALVERIFY << OP_CHECKSIG));

    // Sender provides N pubkeys, receivers provides M signatures
    mTemplates.insert(
        make_pair(TX_MULTISIG, CScript() << OP_SMALLINTEGER << OP_PUBKEYS << OP_SMALLINTEGER << OP_CHECKMULTISIG));
  }

  // Shortcut for pay-to-script-hash, which are more constrained than the other types:
  // it is always OP_HASH160 20 [20 byte hash] OP_EQUAL
  if (scriptPubKey.IsPayToScriptHash()) {
    typeRet = TX_SCRIPTHASH;
    vector<uint8_t> hashBytes(scriptPubKey.begin() + 2, scriptPubKey.begin() + 22);
    vSolutionsRet.push_back(hashBytes);
    return true;
  }

  // Zerocoin
  if (scriptPubKey.IsZerocoinMint()) {
    typeRet = TX_ZEROCOINMINT;
    if (scriptPubKey.size() > 150) return false;
    vector<uint8_t> hashBytes(scriptPubKey.begin() + 2, scriptPubKey.end());
    vSolutionsRet.push_back(hashBytes);
    return true;
  }

  // Provably prunable, data-carrying output
  //
  // So long as script passes the IsUnspendable() test and all but the first
  // byte passes the IsPushOnly() test we don't care what exactly is in the
  // script.
  if (scriptPubKey.size() >= 1 && scriptPubKey[0] == OP_RETURN && scriptPubKey.IsPushOnly(scriptPubKey.begin() + 1)) {
    typeRet = TX_NULL_DATA;
    return true;
  }

  // Scan templates
  const CScript& script1 = scriptPubKey;
  for (const auto& tplate : mTemplates) {
    const CScript& script2 = tplate.second;
    vSolutionsRet.clear();

    opcodetype opcode1, opcode2;
    vector<uint8_t> vch1, vch2;

    // Compare
    auto pc1 = script1.begin();
    auto pc2 = script2.begin();
    while (true) {
      if (pc1 == script1.end() && pc2 == script2.end()) {
        // Found a match
        typeRet = tplate.first;
        if (typeRet == TX_MULTISIG) {
          // Additional checks for TX_MULTISIG:
          uint8_t m = vSolutionsRet.front()[0];
          uint8_t n = vSolutionsRet.back()[0];
          if (m < 1 || n < 1 || m > n || vSolutionsRet.size() - 2 != n) return false;
        }
        return true;
      }
      if (!script1.GetOp(pc1, opcode1, vch1)) break;
      if (!script2.GetOp(pc2, opcode2, vch2)) break;

      // Template matching opcodes:
      if (opcode2 == OP_PUBKEYS) {
        while (vch1.size() >= 33 && vch1.size() <= 65) {
          vSolutionsRet.push_back(vch1);
          if (!script1.GetOp(pc1, opcode1, vch1)) break;
        }
        if (!script2.GetOp(pc2, opcode2, vch2)) break;
        // Normal situation is to fall through
        // to other if/else statements
      }

      if (opcode2 == OP_PUBKEY) {
        if (vch1.size() < 33 || vch1.size() > 65) break;
        vSolutionsRet.push_back(vch1);
      } else if (opcode2 == OP_PUBKEYHASH) {
        if (vch1.size() != sizeof(uint160)) break;
        vSolutionsRet.push_back(vch1);
      } else if (opcode2 == OP_SMALLINTEGER) {  // Single-byte small integer pushed onto vSolutions
        if (opcode1 == OP_0 || (opcode1 >= OP_1 && opcode1 <= OP_16)) {
          auto n = (char)CScript::DecodeOP_N(opcode1);
          vSolutionsRet.push_back(valtype(1, n));
        } else
          break;
      } else if (opcode1 != opcode2 || vch1 != vch2) {
        // Others must match exactly
        break;
      }
    }
  }

  vSolutionsRet.clear();
  typeRet = TX_NONSTANDARD;
  return false;
}

int ScriptSigArgsExpected(txnouttype t, const std::vector<std::vector<uint8_t> >& vSolutions) {
  switch (t) {
    case TX_NONSTANDARD:
    case TX_NULL_DATA:
    case TX_ZEROCOINMINT:
      return -1;
    case TX_PUBKEY:
      return 1;
    case TX_PUBKEYHASH:
      return 2;
    case TX_MULTISIG:
      if (vSolutions.size() < 1 || vSolutions[0].size() < 1) return -1;
      return vSolutions[0][0] + 1;
    case TX_SCRIPTHASH:
      return 1;  // doesn't include args needed by the script
  }
  return -1;
}

bool IsStandard(const CScript& scriptPubKey, txnouttype& whichType) {
  vector<valtype> vSolutions;
  if (!Solver(scriptPubKey, whichType, vSolutions)) return false;

  if (whichType == TX_MULTISIG) {
    uint8_t m = vSolutions.front()[0];
    uint8_t n = vSolutions.back()[0];
    // Support up to x-of-3 multisig txns as standard
    if (n < 1 || n > 3) return false;
    if (m < 1 || m > n) return false;
  } else if (whichType == TX_NULL_DATA &&
             (!GetBoolArg("-datacarrier", true) || scriptPubKey.size() > nMaxDatacarrierBytes))
    return false;

  return whichType != TX_NONSTANDARD;
}

bool ExtractDestination(const CScript& scriptPubKey, CTxDestination& addressRet) {
  vector<valtype> vSolutions;
  txnouttype whichType;
  if (!Solver(scriptPubKey, whichType, vSolutions)) return false;

  if (whichType == TX_PUBKEY) {
    CPubKey pubKey(vSolutions[0]);
    if (!pubKey.IsValid()) return false;

    addressRet = pubKey.GetID();
    return true;
  } else if (whichType == TX_PUBKEYHASH) {
    addressRet = CKeyID(uint160(vSolutions[0]));
    return true;
  } else if (whichType == TX_SCRIPTHASH) {
    addressRet = CScriptID(uint160(vSolutions[0]));
    return true;
  }
  // Multisig txns have more than one address...
  return false;
}

bool ExtractDestinations(const CScript& scriptPubKey, txnouttype& typeRet, vector<CTxDestination>& addressRet,
                         int& nRequiredRet) {
  addressRet.clear();
  typeRet = TX_NONSTANDARD;
  vector<valtype> vSolutions;
  if (!Solver(scriptPubKey, typeRet, vSolutions)) return false;
  if (typeRet == TX_NULL_DATA) {
    // This is data, not addresses
    return false;
  }

  if (typeRet == TX_MULTISIG) {
    nRequiredRet = vSolutions.front()[0];
    for (uint32_t i = 1; i < vSolutions.size() - 1; i++) {
      CPubKey pubKey(vSolutions[i]);
      if (!pubKey.IsValid()) continue;

      CTxDestination address = pubKey.GetID();
      addressRet.push_back(address);
    }

    if (addressRet.empty()) return false;
  } else {
    nRequiredRet = 1;
    CTxDestination address;
    if (!ExtractDestination(scriptPubKey, address)) return false;
    addressRet.push_back(address);
  }

  return true;
}

namespace {
class CScriptVisitor : public mpark::variant<bool> {
 private:
  CScript* script;

 public:
  CScriptVisitor(CScript* scriptin) { script = scriptin; }

  bool operator()(const CNoDestination& dest) const {
    script->clear();
    return false;
  }

  bool operator()(const CKeyID& keyID) const {
    script->clear();
    *script << OP_DUP << OP_HASH160 << ToByteVector(keyID) << OP_EQUALVERIFY << OP_CHECKSIG;
    return true;
  }

  bool operator()(const CScriptID& scriptID) const {
    script->clear();
    *script << OP_HASH160 << ToByteVector(scriptID) << OP_EQUAL;
    return true;
  }
};
}  // namespace

CScript GetScriptForDestination(const CTxDestination& dest) {
  CScript script;

  mpark::visit(CScriptVisitor(&script), dest);
  return script;
}

CScript GetScriptForRawPubKey(const CPubKey& pubKey) {
  return CScript() << std::vector<uint8_t>(pubKey.begin(), pubKey.end()) << OP_CHECKSIG;
}

CScript GetScriptForMultisig(int nRequired, const std::vector<CPubKey>& keys) {
  CScript script;

  script << CScript::EncodeOP_N(nRequired);
  for (const CPubKey& key : keys) script << ToByteVector(key);
  script << CScript::EncodeOP_N(keys.size()) << OP_CHECKMULTISIG;
  return script;
}
bool IsValidDestination(const CTxDestination& dest) {
  return !mpark::holds_alternative<CNoDestination>(dest);
}
