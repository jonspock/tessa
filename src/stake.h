// Copyright (c) 2017-2018 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "amount.h"
#include "primitives/transaction.h"

class uint256;
class CBlockIndex;
class CTxIn;
class CTxOut;
class CKeyStore;
class CWallet;
class CWalletTx;
class CDataStream;


class CStake {
 private:
  CBlockIndex* pindexFrom;
  CTransaction txFrom;
  uint32_t nPosition;

 public:
  CStake() { this->pindexFrom = nullptr; }

  bool SetInput(CTransaction txPrev, uint32_t n);

  CBlockIndex* GetIndexFrom();
  bool GetTxFrom(CTransaction& tx);
  CAmount GetValue();
  bool GetModifier(uint64_t& nStakeModifier);
  CDataStream GetUniqueness();
  bool CreateTxIn(CWallet* pwallet, CTxIn& txIn, uint256 hashTxOut);
  bool CreateTxOuts(CWallet* pwallet, std::vector<CTxOut>& vout, CAmount nTotal);
  bool IsZKP() { return false; }
};
