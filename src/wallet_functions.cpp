// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The ClubChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include "wallet_functions.h"
#include "chainparams.h"
#include "utilstrencodings.h"

bool MoneyRange(CAmount nValueOut) { return nValueOut >= 0 && nValueOut <= Params().MaxMoneyOut(); }

void ReadOrderPos(int64_t& nOrderPos, mapValue_t& mapValue) {
  if (!mapValue.count("n")) {
    nOrderPos = -1;  // TODO: calculate elsewhere
    return;
  }
    nOrderPos = std::atoi(mapValue["n"].c_str());
}

void WriteOrderPos(const int64_t& nOrderPos, mapValue_t& mapValue) {
  if (nOrderPos == -1) return;
  mapValue["n"] = std::to_string(nOrderPos);
}
