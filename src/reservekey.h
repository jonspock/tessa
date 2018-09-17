// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

namespace ecdsa {
class CPubKey;
}

class CWallet;

/** A key allocated from the key pool. */
class CReserveKey {
 protected:
  CWallet* pwallet;
  int64_t nIndex;
  ecdsa::CPubKey vchPubKey;

 public:
  CReserveKey(CWallet* pwalletIn) {
    nIndex = -1;
    pwallet = pwalletIn;
  }

  ~CReserveKey() { ReturnKey(); }

  void ReturnKey();
  bool GetReservedKey(ecdsa::CPubKey& pubkey);
  void KeepKey();
};
