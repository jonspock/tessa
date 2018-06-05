// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The ClubChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "mainzero.h"
#include "accumulatormap.h"
#include "accumulators.h"
#include "primitives/zerocoin.h"
#include "utilmoneystr.h"
#include "zerochain.h"

#include <sstream>

using namespace std;
using namespace libzerocoin;

// ppcoin: total coin age spent in transaction, in the unit of coin-days.
// Only those coins meeting minimum age requirement counts. As those
// transactions not in main chain are not currently indexed so we
// might not find out about their coin age. Older transactions are
// guaranteed to be in main chain by sync-checkpoint. This rule is
// introduced to help nodes establish a consistent view of the coin
// age (trust score) of competing branches.
bool GetCoinAge(const CTransaction& tx, const unsigned int nTxTime, uint64_t& nCoinAge) {
  uint256 bnCentSecond = 0;  // coin age in the unit of cent-seconds
  nCoinAge = 0;

  CBlockIndex* pindex = NULL;
  for (const CTxIn& txin : tx.vin) {
    // First try finding the previous transaction in database
    CTransaction txPrev;
    uint256 hashBlockPrev;
    if (!GetTransaction(txin.prevout.hash, txPrev, hashBlockPrev, true)) {
      LogPrintf("GetCoinAge: failed to find vin transaction \n");
      continue;  // previous transaction not in main chain
    }

    BlockMap::iterator it = mapBlockIndex.find(hashBlockPrev);
    if (it != mapBlockIndex.end())
      pindex = it->second;
    else {
      LogPrintf("GetCoinAge() failed to find block index \n");
      continue;
    }

    // Read block header
    CBlockHeader prevblock = pindex->GetBlockHeader();

    if (prevblock.nTime + nStakeMinAge > nTxTime) continue;  // only count coins meeting min age requirement

    if (nTxTime < prevblock.nTime) {
      LogPrintf("GetCoinAge: Timestamp Violation: txtime less than txPrev.nTime");
      return false;  // Transaction timestamp violation
    }

    int64_t nValueIn = txPrev.vout[txin.prevout.n].nValue;
    bnCentSecond += uint256(nValueIn) * (nTxTime - prevblock.nTime);
  }

  uint256 bnCoinDay = bnCentSecond / COIN / (24 * 60 * 60);
  LogPrintf("coin age bnCoinDay=%s\n", bnCoinDay.ToString().c_str());
  nCoinAge = bnCoinDay.GetCompact();
  return true;
}

bool CheckZerocoinMint(const uint256& txHash, const CTxOut& txout, CValidationState& state, bool fCheckOnly) {
  PublicCoin pubCoin;
  if (!TxOutToPublicCoin(txout, pubCoin, state))
    return state.DoS(100, error("CheckZerocoinMint(): TxOutToPublicCoin() failed"));

    if (!pubCoin.validate()) {
        return state.DoS(100, error("CheckZerocoinMint() : PubCoin does not validate"));
    }

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
  // Check to see if the zZZZ is properly signed
  if (pindex->nHeight >= Params().Zerocoin_StartHeight()) {
    if (!spend.HasValidSignature()) return error("%s: V2 zZZZ spend does not have a valid signature", __func__);
  }

  // Reject serial's that are already in the blockchain
  int nHeightTx = 0;
  if (IsSerialInBlockchain(spend.getCoinSerialNumber(), nHeightTx))
    return error("%s : zZZZ spend with serial %s is already in block %d\n", __func__,
                 spend.getCoinSerialNumber().GetHex(), nHeightTx);
  return true;
}

bool CheckZerocoinSpend(const CTransaction& tx, bool fVerifySignature, CValidationState& state) {
  // max needed non-mint outputs should be 2 - one for redemption address and a possible 2nd for change
  if (tx.vout.size() > 2) {
    int outs = 0;
    for (const CTxOut out : tx.vout) {
      if (out.IsZerocoinMint()) continue;
      outs++;
    }
    if (outs > 2 && !tx.IsCoinStake())
      return state.DoS(100, error("CheckZerocoinSpend(): over two non-mint outputs in a zerocoinspend transaction"));
  }

  // compute the txout hash that is used for the zerocoinspend signatures
  CMutableTransaction txTemp;
  for (const CTxOut out : tx.vout) { txTemp.vout.push_back(out); }
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

      Accumulator accumulator(Params().Zerocoin_Params(), newSpend.getDenomination(), bnAccumulatorValue);

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

    // Rewrite zZZZ supply
    CBlock block;
    assert(ReadBlockFromDisk(block, pindex));

    list<libzerocoin::CoinDenomination> listDenomsSpent = ZerocoinSpendListFromBlock(block);

    // Reset the supply to previous block
    pindex->mapZerocoinSupply = pindex->pprev->mapZerocoinSupply;

    // Add mints to zZZZ supply
    for (auto denom : libzerocoin::zerocoinDenomList) {
      long nDenomAdded =
          count(pindex->vMintDenominationsInBlock.begin(), pindex->vMintDenominationsInBlock.end(), denom);
      pindex->mapZerocoinSupply.at(denom) += nDenomAdded;
    }

    // Remove spends from zZZZ supply
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
  libzerocoin::ZerocoinParams* p = Params().Zerocoin_Params();
  return (p->accumulatorParams.minCoinValue < value) && (value <= p->accumulatorParams.maxCoinValue) &&
         value.isPrime(p->zkp_iterations);
}
