// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once
#include "primitives/transaction.h"
#include "script/script.h"
#include "script/script_error.h"
#include "coins.h"
/**
 * Closure representing one script verification
 * Note that this stores references to the spending transaction
 */
class CScriptCheck {
 private:
  CScript scriptPubKey;
  const CTransaction* ptxTo;
  unsigned int nIn;
  unsigned int nFlags;
  bool cacheStore;
  ScriptError error;

 public:
  CScriptCheck() : ptxTo(nullptr), nIn(0), nFlags(0), cacheStore(false), error(SCRIPT_ERR_UNKNOWN_ERROR) {}
  CScriptCheck(const CCoins& txFromIn, const CTransaction& txToIn, unsigned int nInIn, unsigned int nFlagsIn,
               bool cacheIn)
      : scriptPubKey(txFromIn.vout[txToIn.vin[nInIn].prevout.n].scriptPubKey),
        ptxTo(&txToIn),
        nIn(nInIn),
        nFlags(nFlagsIn),
        cacheStore(cacheIn),
        error(SCRIPT_ERR_UNKNOWN_ERROR) {}

  bool operator()();

  void swap(CScriptCheck& check) {
    scriptPubKey.swap(check.scriptPubKey);
    std::swap(ptxTo, check.ptxTo);
    std::swap(nIn, check.nIn);
    std::swap(nFlags, check.nFlags);
    std::swap(cacheStore, check.cacheStore);
    std::swap(error, check.error);
  }

  ScriptError GetScriptError() const { return error; }
};
