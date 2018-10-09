// Copyright (c) 2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "zerochain.h"
#include "chainparams.h"
#include "libzerocoin/CoinSpend.h"
#include "libzerocoin/PublicCoin.h"
#include "main.h"
#include "txdb.h"
#include "util.h"
#include "validationstate.h"
#include "zerocoindb.h"

#include "accumulatormap.h"
#include "accumulators.h"
#include "chainparams.h"
#include "primitives/zerocoin.h"
#include "utilmoneystr.h"
#include "utilstrencodings.h"

#include <sstream>

using namespace libzerocoin;

// 6 comes from OPCODE (1) + vch.size() (1) + BIGNUM size (4)
#define SCRIPT_OFFSET 6
// For Script size (BIGNUM/Uint256 size)
#define BIGNUM_SIZE 4

bool BlockToMintValueVector(const CBlock& block, const CoinDenomination denom,
                            std::vector<CBigNum>& vValues) {
  for (const CTransaction& tx : block.vtx) {
    if (!tx.IsZerocoinMint()) continue;

    for (const CTxOut& txOut : tx.vout) {
      if (!txOut.scriptPubKey.IsZerocoinMint()) continue;

      CValidationState state;
      PublicCoin coin;
      if (!TxOutToPublicCoin(txOut, coin, state)) return false;

      if (coin.getDenomination() != denom) continue;

      vValues.push_back(coin.getValue());
    }
  }

  return true;
}

bool BlockToPubcoinList(const CBlock& block, std::list<PublicCoin>& listPubcoins) {
  for (const CTransaction& tx : block.vtx) {
    if (!tx.IsZerocoinMint()) continue;

    // uint256 txHash = tx.GetHash();
    for (const auto& txOut : tx.vout) {
      if (!txOut.scriptPubKey.IsZerocoinMint()) continue;
      CValidationState state;
      PublicCoin pubCoin;
      if (!TxOutToPublicCoin(txOut, pubCoin, state)) return false;

      listPubcoins.emplace_back(pubCoin);
    }
  }

  return true;
}

// return a list of zerocoin mints contained in a specific block
bool BlockToZerocoinMintList(const CBlock& block, std::list<CZerocoinMint>& vMints) {
  for (const CTransaction& tx : block.vtx) {
    if (!tx.IsZerocoinMint()) continue;

    // uint256 txHash = tx.GetHash();
    for (const auto& txOut : tx.vout) {
      if (!txOut.scriptPubKey.IsZerocoinMint()) continue;

      CValidationState state;
      PublicCoin pubCoin;
      if (!TxOutToPublicCoin(txOut, pubCoin, state)) return false;

      // version should not actually matter here since it is just a reference to the pubcoin, not to the privcoin
      uint8_t version = 1;
      CZerocoinMint mint = CZerocoinMint(pubCoin.getDenomination(), pubCoin.getValue(), 0, 0, false, version, nullptr);
      mint.SetTxHash(tx.GetHash());
      vMints.push_back(mint);
    }
  }

  return true;
}

void FindMints(std::vector<CMintMeta> vMintsToFind, std::vector<CMintMeta>& vMintsToUpdate,
               std::vector<CMintMeta>& vMissingMints) {
  // see which mints are in our public zerocoin database. The mint should be here if it exists, unless
  // something went wrong
  for (CMintMeta meta : vMintsToFind) {
    uint256 txHash;
    if (!gpZerocoinDB->ReadCoinMint(meta.hashPubcoin, txHash)) {
      vMissingMints.push_back(meta);
      continue;
    }

    // make sure the txhash and block height meta data are correct for this mint
    CTransaction tx;
    uint256 hashBlock;
    if (!GetTransaction(txHash, tx, hashBlock, true)) {
      LogPrintf("%s : cannot find tx %s\n", __func__, txHash.GetHex());
      vMissingMints.push_back(meta);
      continue;
    }

    if (!mapBlockIndex.count(hashBlock)) {
      LogPrintf("%s : cannot find block %s\n", __func__, hashBlock.GetHex());
      vMissingMints.push_back(meta);
      continue;
    }

    // see if this mint is spent
    uint256 hashTxSpend;
    bool fSpent = gpZerocoinDB->ReadCoinSpend(meta.hashSerial, hashTxSpend);

    // if marked as spent, check that it actually made it into the chain
    CTransaction txSpend;
    uint256 hashBlockSpend;
    if (fSpent && !GetTransaction(hashTxSpend, txSpend, hashBlockSpend, true)) {
      LogPrintf("%s : cannot find spend tx %s\n", __func__, hashTxSpend.GetHex());
      meta.isUsed = false;
      vMintsToUpdate.push_back(meta);
      continue;
    }

    // The mint has been incorrectly labelled as spent in gpZerocoinDB and needs to be undone
    int nHeightTx = 0;
    uint256 hashSerial = meta.hashSerial;
    uint256 txidSpend;
    if (fSpent && !IsSerialInBlockchain(hashSerial, nHeightTx, txidSpend)) {
      LogPrintf("%s : cannot find block %s. Erasing coinspend from zerocoinDB.\n", __func__, hashBlockSpend.GetHex());
      meta.isUsed = false;
      vMintsToUpdate.push_back(meta);
      continue;
    }

    // is the denomination correct?
    for (auto& out : tx.vout) {
      if (!out.IsZerocoinMint()) continue;
      PublicCoin pubcoin;
      CValidationState state;
      TxOutToPublicCoin(out, pubcoin, state);
      if (GetPubCoinHash(pubcoin.getValue()) == meta.hashPubcoin && pubcoin.getDenomination() != meta.denom) {
        LogPrintf("%s: found mismatched denom pubcoinhash = %s\n", __func__, meta.hashPubcoin.GetHex());
        meta.denom = pubcoin.getDenomination();
        vMintsToUpdate.emplace_back(meta);
      }
    }

    // if meta data is correct, then no need to update
    if (meta.txid == txHash && meta.nHeight == mapBlockIndex[hashBlock]->nHeight && meta.isUsed == fSpent) continue;

    // mark this mint for update
    meta.txid = txHash;
    meta.nHeight = mapBlockIndex[hashBlock]->nHeight;
    meta.isUsed = fSpent;
    LogPrint(TessaLog::ZKP, "%s: found updates for pubcoinhash = %s\n", __func__, meta.hashPubcoin.GetHex());

    vMintsToUpdate.push_back(meta);
  }
}

int GetZerocoinStartHeight() { return Params().Zerocoin_StartHeight(); }

bool GetZerocoinMint(const CBigNum& bnPubcoin, uint256& txHash) {
  txHash.SetNull();
  return gpZerocoinDB->ReadCoinMint(bnPubcoin, txHash);
}

bool IsPubcoinInBlockchain(const uint256& hashPubcoin, uint256& txid) {
  txid.SetNull();
  return gpZerocoinDB->ReadCoinMint(hashPubcoin, txid);
}

bool IsSerialKnown(const CBigNum& bnSerial) {
  uint256 txHash;
  return gpZerocoinDB->ReadCoinSpend(bnSerial, txHash);
}

bool IsSerialInBlockchain(const CBigNum& bnSerial, int& nHeightTx) {
  uint256 txHash;
  // if not in gpZerocoinDB then its not in the blockchain
  if (!gpZerocoinDB->ReadCoinSpend(bnSerial, txHash)) return false;

  return IsTransactionInChain(txHash, nHeightTx);
}

bool IsSerialInBlockchain(const uint256& hashSerial, int& nHeightTx, uint256& txidSpend) {
  CTransaction tx;
  return IsSerialInBlockchain(hashSerial, nHeightTx, txidSpend, tx);
}

bool IsSerialInBlockchain(const uint256& hashSerial, int& nHeightTx, uint256& txidSpend, CTransaction& tx) {
  txidSpend.SetNull();
  // if not in gpZerocoinDB then its not in the blockchain
  if (!gpZerocoinDB->ReadCoinSpend(hashSerial, txidSpend)) return false;

  return IsTransactionInChain(txidSpend, nHeightTx, tx);
}

std::string ReindexZerocoinDB() {
  if (!gpZerocoinDB->WipeCoins("spends") || !gpZerocoinDB->WipeCoins("mints")) {
    return _("Failed to wipe zerocoinDB");
  }

  CBlockIndex* pindex = chainActive[Params().Zerocoin_StartHeight()];
  while (pindex) {
    if (pindex->nHeight % 1000 == 0) LogPrint(TessaLog::ZKP, "Reindexing zerocoin : block %d...\n", pindex->nHeight);

    CBlock block;
    if (!ReadBlockFromDisk(block, pindex)) { return _("Reindexing zerocoin failed"); }

    for (const CTransaction& tx : block.vtx) {
      for (const auto& v : tx.vin) {
        (void)v;  // to silence unused variable warning
        if (tx.IsCoinBase()) break;
        if (tx.ContainsZerocoins()) {
          uint256 txid = tx.GetHash();
          // Record Serials
          if (tx.IsZerocoinSpend()) {
            for (auto& in : tx.vin) {
              if (!in.scriptSig.IsZerocoinSpend()) continue;

              CoinSpend spend = TxInToZerocoinSpend(in);
              gpZerocoinDB->WriteCoinSpend(spend.getCoinSerialNumber(), txid);
            }
          }

          // Record mints
          if (tx.IsZerocoinMint()) {
            for (auto& out : tx.vout) {
              if (!out.IsZerocoinMint()) continue;

              CValidationState state;
              PublicCoin coin;
              TxOutToPublicCoin(out, coin, state);
              gpZerocoinDB->WriteCoinMint(coin, txid);
            }
          }
        }
      }
    }
    pindex = chainActive.Next(pindex);
  }

  return "";
}

bool RemoveSerialFromDB(const CBigNum& bnSerial) { return gpZerocoinDB->EraseCoinSpend(bnSerial); }

CoinSpend TxInToZerocoinSpend(const CTxIn& txin) {
  // extract the CoinSpend from the txin
  std::vector<char, zero_after_free_allocator<char> > dataTxIn;
  dataTxIn.insert(dataTxIn.end(), txin.scriptSig.begin() + BIGNUM_SIZE, txin.scriptSig.end());
  CDataStream serializedCoinSpend(dataTxIn, SER_NETWORK, PROTOCOL_VERSION);

  ZerocoinParams* paramsAccumulator = gpZerocoinParams;
  CoinSpend spend(paramsAccumulator, serializedCoinSpend);

  return spend;
}

bool TxOutToPublicCoin(const CTxOut& txout, PublicCoin& pubCoin, CValidationState& state) {
  CBigNum publicZerocoin;
  std::vector<uint8_t> vchZeroMint;
  vchZeroMint.insert(vchZeroMint.end(), txout.scriptPubKey.begin() + SCRIPT_OFFSET,
                     txout.scriptPubKey.begin() + txout.scriptPubKey.size());
  publicZerocoin.setvch(vchZeroMint);

  CoinDenomination denomination = AmountToZerocoinDenomination(txout.nValue);
  LogPrint(TessaLog::ZKP, "%s ZCPRINT denomination %d pubcoin %s\n", __func__, denomination, publicZerocoin.GetHex());
  if (denomination == ZQ_ERROR)
    return state.DoS(100, error("TxOutToPublicCoin : txout.nValue is not correct"));

  PublicCoin checkPubCoin(publicZerocoin, denomination);
  pubCoin = checkPubCoin;

  return true;
}

// return a list of zerocoin spends contained in a specific block, list may have many denominations
std::list<CoinDenomination> ZerocoinSpendListFromBlock(const CBlock& block) {
  std::list<CoinDenomination> vSpends;
  for (const CTransaction& tx : block.vtx) {
    if (!tx.IsZerocoinSpend()) continue;

    for (const CTxIn& txin : tx.vin) {
      if (!txin.scriptSig.IsZerocoinSpend()) continue;

      CoinDenomination c = IntToZerocoinDenomination(txin.nSequence);
      vSpends.push_back(c);
    }
  }
  return vSpends;
}


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
    if (gpZerocoinDB->ReadCoinMint(coin.getValue(), txid) && IsTransactionInChain(txid, nHeight))
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
  std::set<CBigNum> serials;
  std::list<CoinSpend> vSpends;
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
      if (!gpZerocoinDB->ReadAccumulatorValue(newSpend.getAccumulatorChecksum(), bnAccumulatorValue)) {
        uint32_t nChecksum = newSpend.getAccumulatorChecksum();
        return state.DoS(100, error("%s: Zerocoinspend could not find accumulator associated with checksum %s",
                                    __func__, HexStr(BEGIN(nChecksum), END(nChecksum))));
      }

      Accumulator accumulator(gpZerocoinParams, bnAccumulatorValue, newSpend.getDenomination());

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

    std::vector<CoinDenomination> vDenomsBefore = pindex->vMintDenominationsInBlock;
    pindex->vMintDenominationsInBlock.clear();
    for (const auto& mint : listMints) pindex->vMintDenominationsInBlock.emplace_back(mint.GetDenomination());

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

    std::list<CoinDenomination> listDenomsSpent = ZerocoinSpendListFromBlock(block);

    // Reset the supply to previous block
    pindex->mapZerocoinSupply = pindex->pprev->mapZerocoinSupply;

    // Add mints to ZKP supply
    for (auto denom : zerocoinDenomList) {
      long nDenomAdded =
          count(pindex->vMintDenominationsInBlock.begin(), pindex->vMintDenominationsInBlock.end(), denom);
      pindex->mapZerocoinSupply.at(denom) += nDenomAdded;
    }

    // Remove spends from ZKP supply
    for (auto denom : listDenomsSpent) pindex->mapZerocoinSupply.at(denom)--;

    // Rewrite money supply
    assert(gpBlockTreeDB->WriteBlockIndex(CDiskBlockIndex(pindex)));

    if (pindex->nHeight < chainActive.Height())
      pindex = chainActive.Next(pindex);
    else
      break;
  }
}

bool ValidatePublicCoin(const CBigNum& value) {
  ZerocoinParams* p = gpZerocoinParams;
  return (p->accumulatorParams.minCoinValue < value) && (value <= p->accumulatorParams.maxCoinValue) &&
         value.isPrime(p->zkp_iterations);
}
