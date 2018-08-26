// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "mainzero.h"
#include "accumulatormap.h"
#include "accumulators.h"
#include "libzerocoin/CoinSpend.h"
#include "libzerocoin/PublicCoin.h"
#include "primitives/zerocoin.h"
#include "utilmoneystr.h"
#include "zerochain.h"

#include <sstream>

using namespace std;
using namespace libzerocoin;

bool CheckZerocoinMint(const uint256& txHash, const CTxOut& txout, CValidationState& state, bool fCheckOnly) {
  PublicCoin pubCoin;
  if (!TxOutToPublicCoin(txout, pubCoin, state))
    return state.DoS(100, error("CheckZerocoinMint(): TxOutToPublicCoin() failed"));

  if (!pubCoin.validate()) { return state.DoS(100, error("CheckZerocoinMint() : PubCoin does not validate")); }

  return true;
}

bool ContextualCheckZerocoinMint(const CTransaction& tx, const PublicCoin& coin, const CBlockIndex* pindex) {
  if (pindex->nHeight >= Params().Zerocoin_StartHeight() && Params().NetworkID() != CBaseChainParams::TESTNET) {
    // See if this coin has already been added to the blockchain
    uint256 txid;
    int nHeight;
    if (zerocoinDB->ReadCoinMint(coin.getValue(), txid) && IsTransactionInChain(txid, nHeight))
      return error("%s: pubcoin %s was already accumulated in tx %s", __func__, coin.getValue().GetHex().substr(0, 10),
                   txid.GetHex());
  }

  return true;
}

bool ContextualCheckZerocoinSpend(const CTransaction& tx, const CoinSpend& spend, CBlockIndex* pindex,
                                  const uint256& hashBlock) {
  // Check to see if the ZKP is properly signed
  if (pindex->nHeight >= Params().Zerocoin_StartHeight()) {
    if (!spend.HasValidSignature()) return error("%s: V2 ZKP spend does not have a valid signature", __func__);
  }

  // Reject serial's that are already in the blockchain
  int nHeightTx = 0;
  if (IsSerialInBlockchain(spend.getCoinSerialNumber(), nHeightTx))
    return error("%s : ZKP spend with serial %s is already in block %d\n", __func__,
                 spend.getCoinSerialNumber().GetHex(), nHeightTx);
  return true;
}

bool CheckZerocoinSpend(const CTransaction& tx, bool fVerifySignature, CValidationState& state) {
  // max needed non-mint outputs should be 2 - one for redemption address and a possible 2nd for change
  if (tx.vout.size() > 2) {
    int outs = 0;
    for (const CTxOut& out : tx.vout) {
      if (out.IsZerocoinMint()) continue;
      outs++;
    }
    if (outs > 2 && !tx.IsCoinStake())
      return state.DoS(100, error("CheckZerocoinSpend(): over two non-mint outputs in a zerocoinspend transaction"));
  }

  // compute the txout hash that is used for the zerocoinspend signatures
  CMutableTransaction txTemp;
  for (const CTxOut& out : tx.vout) { txTemp.vout.push_back(out); }
  uint256 hashTxOut = txTemp.GetHash();

  bool fValidated = false;
  set<CBigNum> serials;
  list<CoinSpend> vSpends;
  CAmount nTotalRedeemed = 0;
  for (const CTxIn& txin : tx.vin) {
    // only check txin that is a zcspend
    if (!txin.scriptSig.IsZerocoinSpend()) continue;

    CoinSpend newSpend = TxInToZerocoinSpend(txin);
    vSpends.push_back(newSpend);

    // check that the denomination is valid
    if (newSpend.getDenomination() == ZQ_ERROR)
      return state.DoS(100, error("Zerocoinspend does not have the correct denomination"));

    // check that denomination is what it claims to be in nSequence
    if (newSpend.getDenomination() != txin.nSequence)
      return state.DoS(100, error("Zerocoinspend nSequence denomination does not match CoinSpend"));

    // make sure the txout has not changed
    if (newSpend.getTxOutHash() != hashTxOut)
      return state.DoS(100, error("Zerocoinspend does not use the same txout that was used in the SoK"));

    // Skip signature verification during initial block download
    if (fVerifySignature) {
      // see if we have record of the accumulator used in the spend tx
      CBigNum bnAccumulatorValue = 0;
      if (!zerocoinDB->ReadAccumulatorValue(newSpend.getAccumulatorChecksum(), bnAccumulatorValue)) {
        uint32_t nChecksum = newSpend.getAccumulatorChecksum();
        return state.DoS(100, error("%s: Zerocoinspend could not find accumulator associated with checksum %s",
                                    __func__, HexStr(BEGIN(nChecksum), END(nChecksum))));
      }

      Accumulator accumulator(libzerocoin::gpZerocoinParams, newSpend.getDenomination(), bnAccumulatorValue);

      // Check that the coin has been accumulated
      if (!newSpend.Verify(accumulator))
        return state.DoS(100, error("CheckZerocoinSpend(): zerocoin spend did not verify"));
    }

    if (serials.count(newSpend.getCoinSerialNumber()))
      return state.DoS(100, error("Zerocoinspend serial is used twice in the same tx"));
    serials.insert(newSpend.getCoinSerialNumber());

    // make sure that there is no over redemption of coins
    nTotalRedeemed += ZerocoinDenominationToAmount(newSpend.getDenomination());
    fValidated = true;
  }

  if (!tx.IsCoinStake() && nTotalRedeemed < tx.GetValueOut()) {
    LogPrintf("redeemed = %s , spend = %s \n", FormatMoney(nTotalRedeemed), FormatMoney(tx.GetValueOut()));
    return state.DoS(100, error("Transaction spend more than was redeemed in zerocoins"));
  }

  return fValidated;
}

////--------------NOT USED------------------------NOT USED------------------------NOT USED----------

void RecalculateZKPMinted() {
  CBlockIndex* pindex = chainActive[Params().Zerocoin_StartHeight()];
  int nHeightEnd = chainActive.Height();
  while (true) {
    if (pindex->nHeight % 1000 == 0) LogPrintf("%s : block %d...\n", __func__, pindex->nHeight);

    // overwrite possibly wrong vMintsInBlock data
    CBlock block;
    assert(ReadBlockFromDisk(block, pindex));

    std::list<CZerocoinMint> listMints;
    BlockToZerocoinMintList(block, listMints);

    vector<libzerocoin::CoinDenomination> vDenomsBefore = pindex->vMintDenominationsInBlock;
    pindex->vMintDenominationsInBlock.clear();
    for (auto mint : listMints) pindex->vMintDenominationsInBlock.emplace_back(mint.GetDenomination());

    if (pindex->nHeight < nHeightEnd)
      pindex = chainActive.Next(pindex);
    else
      break;
  }
}

void RecalculateZKPSpent() {
  CBlockIndex* pindex = chainActive[Params().Zerocoin_StartHeight()];
  while (true) {
    if (pindex->nHeight % 1000 == 0) LogPrintf("%s : block %d...\n", __func__, pindex->nHeight);

    // Rewrite ZKP supply
    CBlock block;
    assert(ReadBlockFromDisk(block, pindex));

    list<libzerocoin::CoinDenomination> listDenomsSpent = ZerocoinSpendListFromBlock(block);

    // Reset the supply to previous block
    pindex->mapZerocoinSupply = pindex->pprev->mapZerocoinSupply;

    // Add mints to ZKP supply
    for (auto denom : libzerocoin::zerocoinDenomList) {
      long nDenomAdded =
          count(pindex->vMintDenominationsInBlock.begin(), pindex->vMintDenominationsInBlock.end(), denom);
      pindex->mapZerocoinSupply.at(denom) += nDenomAdded;
    }

    // Remove spends from ZKP supply
    for (auto denom : listDenomsSpent) pindex->mapZerocoinSupply.at(denom)--;

    // Rewrite money supply
    assert(pblocktree->WriteBlockIndex(CDiskBlockIndex(pindex)));

    if (pindex->nHeight < chainActive.Height())
      pindex = chainActive.Next(pindex);
    else
      break;
  }
}

bool ValidatePublicCoin(const CBigNum& value) {
  libzerocoin::ZerocoinParams* p = gpZerocoinParams;
  return (p->accumulatorParams.minCoinValue < value) && (value <= p->accumulatorParams.maxCoinValue) &&
         value.isPrime(p->zkp_iterations);
}
