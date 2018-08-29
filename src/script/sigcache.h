// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "script/interpreter.h"
#include "ecdsa/key.h"

#include <vector>


class CachingTransactionSignatureChecker : public TransactionSignatureChecker {
 private:
  bool store;

 public:
  CachingTransactionSignatureChecker(const CTransaction* txToIn, unsigned int nInIn, bool storeIn = true)
      : TransactionSignatureChecker(txToIn, nInIn), store(storeIn) {}

  bool VerifySignature(const std::vector<uint8_t>& vchSig, const ecdsa::CPubKey& vchPubKey, const uint256& sighash) const;
};

