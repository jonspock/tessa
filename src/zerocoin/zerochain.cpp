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

// 6 comes from OPCODE (1) + vch.size() (1) + BIGNUM size (4)
#define SCRIPT_OFFSET 6
// For Script size (BIGNUM/Uint256 size)
#define BIGNUM_SIZE 4

bool BlockToMintValueVector(const CBlock& block, const libzerocoin::CoinDenomination denom,
                            std::vector<CBigNum>& vValues) {
  for (const CTransaction& tx : block.vtx) {
    if (!tx.IsZerocoinMint()) continue;

    for (const CTxOut& txOut : tx.vout) {
      if (!txOut.scriptPubKey.IsZerocoinMint()) continue;

      CValidationState state;
      libzerocoin::PublicCoin coin;
      if (!TxOutToPublicCoin(txOut, coin, state)) return false;

      if (coin.getDenomination() != denom) continue;

      vValues.push_back(coin.getValue());
    }
  }

  return true;
}

bool BlockToPubcoinList(const CBlock& block, std::list<libzerocoin::PublicCoin>& listPubcoins) {
  for (const CTransaction& tx : block.vtx) {
    if (!tx.IsZerocoinMint()) continue;

    // uint256 txHash = tx.GetHash();
    for (const auto& txOut : tx.vout) {
      if (!txOut.scriptPubKey.IsZerocoinMint()) continue;
      CValidationState state;
      libzerocoin::PublicCoin pubCoin;
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
      libzerocoin::PublicCoin pubCoin;
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
      libzerocoin::PublicCoin pubcoin;
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

              libzerocoin::CoinSpend spend = TxInToZerocoinSpend(in);
              gpZerocoinDB->WriteCoinSpend(spend.getCoinSerialNumber(), txid);
            }
          }

          // Record mints
          if (tx.IsZerocoinMint()) {
            for (auto& out : tx.vout) {
              if (!out.IsZerocoinMint()) continue;

              CValidationState state;
              libzerocoin::PublicCoin coin;
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

libzerocoin::CoinSpend TxInToZerocoinSpend(const CTxIn& txin) {
  // extract the CoinSpend from the txin
  std::vector<char, zero_after_free_allocator<char> > dataTxIn;
  dataTxIn.insert(dataTxIn.end(), txin.scriptSig.begin() + BIGNUM_SIZE, txin.scriptSig.end());
  CDataStream serializedCoinSpend(dataTxIn, SER_NETWORK, PROTOCOL_VERSION);

  libzerocoin::ZerocoinParams* paramsAccumulator = libzerocoin::gpZerocoinParams;
  libzerocoin::CoinSpend spend(paramsAccumulator, serializedCoinSpend);

  return spend;
}

bool TxOutToPublicCoin(const CTxOut& txout, libzerocoin::PublicCoin& pubCoin, CValidationState& state) {
  CBigNum publicZerocoin;
  std::vector<uint8_t> vchZeroMint;
  vchZeroMint.insert(vchZeroMint.end(), txout.scriptPubKey.begin() + SCRIPT_OFFSET,
                     txout.scriptPubKey.begin() + txout.scriptPubKey.size());
  publicZerocoin.setvch(vchZeroMint);

  libzerocoin::CoinDenomination denomination = libzerocoin::AmountToZerocoinDenomination(txout.nValue);
  LogPrint(TessaLog::ZKP, "%s ZCPRINT denomination %d pubcoin %s\n", __func__, denomination, publicZerocoin.GetHex());
  if (denomination == libzerocoin::ZQ_ERROR)
    return state.DoS(100, error("TxOutToPublicCoin : txout.nValue is not correct"));

  libzerocoin::PublicCoin checkPubCoin(publicZerocoin, denomination);
  pubCoin = checkPubCoin;

  return true;
}

// return a list of zerocoin spends contained in a specific block, list may have many denominations
std::list<libzerocoin::CoinDenomination> ZerocoinSpendListFromBlock(const CBlock& block) {
  std::list<libzerocoin::CoinDenomination> vSpends;
  for (const CTransaction& tx : block.vtx) {
    if (!tx.IsZerocoinSpend()) continue;

    for (const CTxIn& txin : tx.vin) {
      if (!txin.scriptSig.IsZerocoinSpend()) continue;

      libzerocoin::CoinDenomination c = libzerocoin::IntToZerocoinDenomination(txin.nSequence);
      vSpends.push_back(c);
    }
  }
  return vSpends;
}
