// Copyright (c) 2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "zerochain.h"

// For EraseMints/Spends
#include "wallet/wallet.h"
#include "wallet/wallettx.h"
#include "wallet_externs.h"

#include "init.h"  // for ShutdownReq

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

bool BlockToMintValueVector(const CBlock& block, const CoinDenomination denom, std::vector<CBigNum>& vValues) {
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
  if (denomination == ZQ_ERROR) return state.DoS(100, error("TxOutToPublicCoin : txout.nValue is not correct"));

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

bool EraseZerocoinSpendsInTx(const std::vector<CTxIn>& vin) {
  // erase all zerocoinspends in this transaction
  for (const CTxIn& txin : vin) {
    if (txin.scriptSig.IsZerocoinSpend()) {
      CoinSpend spend = TxInToZerocoinSpend(txin);
      if (!gpZerocoinDB->EraseCoinSpend(spend.getCoinSerialNumber()))
        return error("failed to erase spent zerocoin in block");

      // if this was our spend, then mark it unspent now
      if (pwalletMain) {
        if (pwalletMain->IsMyZerocoinSpend(spend.getCoinSerialNumber())) {
          if (!pwalletMain->SetMintUnspent(spend.getCoinSerialNumber()))
            LogPrintf("%s: failed to automatically reset mint", __func__);
        }
      }
    }
  }
  return true;
}
bool EraseZerocoinMintsInTx(const std::vector<CTxOut>& vout, CValidationState& state) {
  // erase all zerocoinmints in this transaction
  for (const CTxOut& txout : vout) {
    if (txout.scriptPubKey.empty() || !txout.scriptPubKey.IsZerocoinMint()) continue;

    PublicCoin pubCoin;
    if (!TxOutToPublicCoin(txout, pubCoin, state)) return error("DisconnectBlock(): TxOutToPublicCoin() failed");

    if (!gpZerocoinDB->EraseCoinMint(pubCoin.getValue())) return error("DisconnectBlock(): Failed to erase coin mint");
  }
  return true;
}

bool ReindexAccumulators(std::list<uint256>& listMissingCheckpoints, std::string& strError) {
  // Tessa: recalculate Accumulator Checkpoints that failed to database properly
  if (!listMissingCheckpoints.empty() && chainActive.Height() >= Params().Zerocoin_StartHeight()) {
    LogPrintf("%s : finding missing checkpoints\n", __func__);

    // search the chain to see when zerocoin started
    int nZerocoinStart = Params().Zerocoin_StartHeight();

    // find each checkpoint that is missing
    CBlockIndex* pindex = chainActive[nZerocoinStart];
    while (pindex) {
      interruption_point(ShutdownRequested());
      // if (ShutdownRequested()) return false;

      // find checkpoints by iterating through the blockchain beginning with the first zerocoin block
      if (pindex->nAccumulatorCheckpoint != pindex->pprev->nAccumulatorCheckpoint) {
        if (find(listMissingCheckpoints.begin(), listMissingCheckpoints.end(), pindex->nAccumulatorCheckpoint) !=
            listMissingCheckpoints.end()) {
          uint256 nCheckpointCalculated;
          AccumulatorMap mapAccumulators(libzerocoin::gpZerocoinParams);
          if (!CalculateAccumulatorCheckpoint(pindex->nHeight, nCheckpointCalculated, mapAccumulators)) {
            // GetCheckpoint could have terminated due to a shutdown request. Check this here.
            if (ShutdownRequested()) break;
            strError = _("Failed to calculate accumulator checkpoint");
            return error("%s: %s", __func__, strError);
          }

          // check that the calculated checkpoint is what is in the index.
          if (nCheckpointCalculated != pindex->nAccumulatorCheckpoint) {
            LogPrintf("%s : height=%d calculated_checkpoint=%s actual=%s\n", __func__, pindex->nHeight,
                      nCheckpointCalculated.GetHex(), pindex->nAccumulatorCheckpoint.GetHex());
            strError = _("Calculated accumulator checkpoint is not what is recorded by block index");
            return error("%s: %s", __func__, strError);
          }

          DatabaseChecksums(mapAccumulators);
          auto it = find(listMissingCheckpoints.begin(), listMissingCheckpoints.end(), pindex->nAccumulatorCheckpoint);
          listMissingCheckpoints.erase(it);
        }
      }
      pindex = chainActive.Next(pindex);
    }
  }
  return true;
}

bool UpdateZKPSupply(const CBlock& block, CBlockIndex* pindex, bool fJustCheck) {
  std::list<CZerocoinMint> listMints;
  BlockToZerocoinMintList(block, listMints);
  std::list<libzerocoin::CoinDenomination> listSpends = ZerocoinSpendListFromBlock(block);

  // Initialize zerocoin supply to the supply from previous block
  if (pindex->pprev && pindex->pprev->GetBlockHeader().nHeaderVersion > (int32_t)BlockVersion::GENESIS_BLOCK_VERSION) {
    for (auto& denom : zerocoinDenomList) {
      pindex->mapZerocoinSupply.at(denom) = pindex->pprev->mapZerocoinSupply.at(denom);
    }
  }

  // Track zerocoin money supply
  CAmount nAmountZerocoinSpent = 0;
  pindex->vMintDenominationsInBlock.clear();
  if (pindex->pprev) {
    std::set<uint256> setAddedToWallet;
    for (auto& m : listMints) {
      libzerocoin::CoinDenomination denom = m.GetDenomination();
      pindex->vMintDenominationsInBlock.push_back(m.GetDenomination());
      pindex->mapZerocoinSupply.at(denom)++;

      // Remove any of our own mints from the mintpool (unless just checking)
      if (!fJustCheck && pwalletMain) {
        if (pwalletMain->IsMyMint(m.GetValue())) {
          pwalletMain->UpdateMint(m.GetValue(), pindex->nHeight, m.GetTxHash(), m.GetDenomination());

          // Add the transaction to the wallet
          for (auto& tx : block.vtx) {
            uint256 txid = tx.GetHash();
            if (setAddedToWallet.count(txid)) continue;
            if (txid == m.GetTxHash()) {
              CWalletTx wtx(pwalletMain, tx);
              wtx.nTimeReceived = block.GetBlockTime();
              wtx.SetMerkleBranch(block);
              pwalletMain->AddToWallet(wtx);
              setAddedToWallet.insert(txid);
            }
          }
        }
      }
    }

    for (auto& denom : listSpends) {
      pindex->mapZerocoinSupply.at(denom)--;
      nAmountZerocoinSpent += libzerocoin::ZerocoinDenominationToAmount(denom);

      // zerocoin failsafe
      if (pindex->mapZerocoinSupply.at(denom) < 0)
        return error("Block contains zerocoins that spend more than are in the available supply to spend");
    }
  }

  // for (auto& denom : zerocoinDenomList)
  //   LogPrint(TessaLog::ZKP, "%s coins for denomination %d pubcoin %s\n", __func__, denom,
  //   pindex->mapZerocoinSupply.at(denom));

  return true;
}
// Record ZKP serials
bool RecordZKPSerials(const std::vector<std::pair<CoinSpend, uint256> >& vSpends, const CBlock& block,
                      const CBlockIndex* pindex, CValidationState& state) {
  if (pwalletMain) {
    std::set<uint256> setAddedTx;
    for (auto& pSpend : vSpends) {
      // record spend to database
      if (!gpZerocoinDB->WriteCoinSpend(pSpend.first.getCoinSerialNumber(), pSpend.second))
        return state.Abort(("Failed to record coin serial to database"));

      // Send signal to wallet if this is ours
      if (pwalletMain->IsMyZerocoinSpend(pSpend.first.getCoinSerialNumber())) {
        LogPrintf("%s: %s detected zerocoinspend in transaction %s \n", __func__,
                  pSpend.first.getCoinSerialNumber().GetHex(), pSpend.second.GetHex());
        pwalletMain->NotifyZerocoinChanged.fire(pwalletMain, pSpend.first.getCoinSerialNumber().GetHex(), "Used",
                                                CT_UPDATED);

        // Don't add the same tx multiple times
        if (setAddedTx.count(pSpend.second)) continue;
        
        // Search block for matching tx, turn into wtx, set merkle branch, add to wallet
        for (const CTransaction& tx : block.vtx) {
          if (tx.GetHash() == pSpend.second) {
            CWalletTx wtx(pwalletMain, tx);
            wtx.nTimeReceived = pindex->GetBlockTime();
            wtx.SetMerkleBranch(block);
            pwalletMain->AddToWallet(wtx);
            setAddedTx.insert(pSpend.second);
          }
        }
      }
    }
  }
  return true;
}
bool UpdateZerocoinVectors(const CTransaction& tx, const uint256& hashBlock, std::vector<uint256>& vSpendsInBlock,
                           std::vector<std::pair<CoinSpend, uint256> >& vSpends,
                           std::vector<std::pair<PublicCoin, uint256> >& vMints, CBlockIndex* pindex, CAmount& nValueIn,
                           CValidationState& state) {
  if (tx.IsZerocoinSpend()) {
    int nHeightTx = 0;
    uint256 txid = tx.GetHash();
    vSpendsInBlock.emplace_back(txid);
    if (IsTransactionInChain(txid, nHeightTx)) {
      // when verifying blocks on init, the blocks are scanned without being disconnected - prevent that from causing
      // an error
      if (!fVerifyingBlocks || (fVerifyingBlocks && pindex->nHeight > nHeightTx))
        return state.DoS(100,
                         error("%s : txid %s already exists in block %d , trying to include it again in block %d",
                               __func__, tx.GetHash().GetHex(), nHeightTx, pindex->nHeight),
                         REJECT_INVALID, "bad-txns-inputs-missingorspent");
    }

    // Check for double spending of serial #'s
    std::set<CBigNum> setSerials;
    for (const CTxIn& txIn : tx.vin) {
      if (!txIn.scriptSig.IsZerocoinSpend()) continue;
      CoinSpend spend = TxInToZerocoinSpend(txIn);
      nValueIn += spend.getDenomination() * COIN;
      // queue for db write after the 'justcheck' section has concluded
      vSpends.emplace_back(std::make_pair(spend, tx.GetHash()));
      if (!ContextualCheckZerocoinSpend(tx, spend, pindex, hashBlock))
        return state.DoS(
            100, error("%s: failed to add block %s with invalid zerocoinspend", __func__, tx.GetHash().GetHex()),
            REJECT_INVALID);
    }

    // Check that ZKP mints are not already known
    if (tx.IsZerocoinMint()) {
      for (auto& out : tx.vout) {
        if (!out.IsZerocoinMint()) continue;

        PublicCoin coin;
        if (!TxOutToPublicCoin(out, coin, state))
          return state.DoS(100,
                           error("%s: failed final check of zerocoinmint for tx %s", __func__, tx.GetHash().GetHex()));

        if (!ContextualCheckZerocoinMint(tx, coin, pindex))
          return state.DoS(100, error("%s: zerocoin mint failed contextual check", __func__));

        vMints.emplace_back(std::make_pair(coin, tx.GetHash()));
      }
    }
  }
  return true;
}
