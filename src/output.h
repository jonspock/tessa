// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The ClubChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once
#include "amount.h"
#include <string>
class CWalletTx;

class COutput {
 public:
  const CWalletTx* tx;
  int i;
  int nDepth;
  bool fSpendable;

  COutput(const CWalletTx* txIn, int iIn, int nDepthIn, bool fSpendableIn) {
    tx = txIn;
    i = iIn;
    nDepth = nDepthIn;
    fSpendable = fSpendableIn;
  }

  CAmount Value() const;

  std::string ToString() const;
};
