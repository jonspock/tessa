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

class CStakeInput {
 protected:
  CBlockIndex* pindexFrom;

 public:
  virtual ~CStakeInput(){};
  virtual CBlockIndex* GetIndexFrom() = 0;
  virtual bool CreateTxIn(CWallet* pwallet, CTxIn& txIn, uint256 hashTxOut) = 0;
  virtual bool GetTxFrom(CTransaction& tx) = 0;
  virtual CAmount GetValue() = 0;
  virtual bool CreateTxOuts(CWallet* pwallet, std::vector<CTxOut>& vout, CAmount nTotal) = 0;
  virtual bool GetModifier(uint64_t& nStakeModifier) = 0;
  virtual bool IsZKP() = 0;
  virtual CDataStream GetUniqueness() = 0;
};

class CStake : public CStakeInput {
 private:
  CTransaction txFrom;
  uint32_t nPosition;

 public:
  CStake() { this->pindexFrom = nullptr; }

  bool SetInput(CTransaction txPrev, uint32_t n);

  CBlockIndex* GetIndexFrom() override;
  bool GetTxFrom(CTransaction& tx) override;
  CAmount GetValue() override;
  bool GetModifier(uint64_t& nStakeModifier) override;
  CDataStream GetUniqueness() override;
  bool CreateTxIn(CWallet* pwallet, CTxIn& txIn, uint256 hashTxOut) override;
  bool CreateTxOuts(CWallet* pwallet, std::vector<CTxOut>& vout, CAmount nTotal) override;
  bool IsZKP() override { return false; }
};
