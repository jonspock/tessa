// Copyright (c) 2017-2018 The PIVX developers
// Copyright (c) 2018 The ClubChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "zerowallet.h"
#include "init.h"
#include "libzerocoin/PrivateCoin.h"
#include "main.h"
#include "primitives/deterministicmint.h"
#include "txdb.h"
#include "uint512.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "zerochain.h"

using namespace std;

#ifdef DEBUG
const int ZMINTS_TO_ADD = 1;
#else
const int ZMINTS_TO_ADD = 20;
#endif

CZeroWallet::CZeroWallet() {
  // Don't try to do anything if the wallet is locked.
  if (pwalletMain->IsLocked()) {
    seedMaster.SetNull();
    nCountLastUsed = 0;
    this->mintPool = CMintPool();
    return;
  }

  // First time running, generate master seed
  uint256 hashSeed;
  uint256 seed;
  bool fFirstRun = !gWalletDB.ReadCurrentSeedHash(hashSeed);
  if (fFirstRun) {
    // Borrow random generator from the key class so that we don't have to worry about randomness
    CKey key;
    key.MakeNewKey(true);
    seed = key.GetPrivKey_256();
    seedMaster = seed;
    LogPrint(ClubLog::ZERO, "%s: first run of zkp wallet detected, new seed generated. Seedhash=%s\n", __func__,
             Hash(seed.begin(), seed.end()).GetHex());
  } else if (!pwalletMain->GetDeterministicSeed(hashSeed, seed)) {
    LogPrintf("%s: failed to get deterministic seed for hashseed %s\n", __func__, hashSeed.GetHex());
    return;
  }

  if (!SetMasterSeed(seed)) {
    LogPrintf("%s: failed to save deterministic seed for hashseed %s\n", __func__, hashSeed.GetHex());
    return;
  }
  this->mintPool = CMintPool(nCountLastUsed);
}

bool CZeroWallet::SetMasterSeed(const uint256& seedMaster, bool fResetCount) {
  if (pwalletMain->IsLocked()) return false;

  if (!seedMaster.IsNull() && !pwalletMain->AddDeterministicSeed(seedMaster)) {
    return error("%s: failed to set master seed.", __func__);
  }

  this->seedMaster = seedMaster;

  nCountLastUsed = 0;

  if (fResetCount)
    gWalletDB.WriteZKPCount(nCountLastUsed);
  else if (!gWalletDB.ReadZKPCount(nCountLastUsed))
    nCountLastUsed = 0;

  mintPool.Reset();

  return true;
}

void CZeroWallet::Lock() { seedMaster.SetNull(); }

void CZeroWallet::AddToMintPool(const std::pair<uint256, uint32_t>& pMint, bool fVerbose) {
  mintPool.Add(pMint, fVerbose);
}

// Add the next ZMINTS_TO_ADD mints to the mint pool
void CZeroWallet::GenerateZMintPool(uint32_t nCountStart, uint32_t nCountEnd) {
  // Is locked
  if (seedMaster.IsNull()) return;

  uint32_t n = nCountLastUsed + 1;

  if (nCountStart > 0) n = nCountStart;

  uint32_t nStop = n + ZMINTS_TO_ADD;
  if (nCountEnd > 0) nStop = std::max(n, n + nCountEnd);

  bool fFound;

  uint256 hashSeed = Hash(seedMaster.begin(), seedMaster.end());
  LogPrint(ClubLog::ZERO, "%s : n=%d nStop=%d, diff = %d\n", __func__, n, nStop - 1, nStop - n);
  int64_t nTime_ref = GetTimeMillis();
  for (uint32_t i = n; i < nStop; ++i) {
    if (ShutdownRequested()) return;

    fFound = false;

    // Prevent unnecessary repeated minted
    for (auto& pair : mintPool) {
      if (pair.second == i) {
        fFound = true;
        break;
      }
    }

    if (fFound) continue;
    int64_t nTime_delta = GetTimeMillis();

    uint512 seedZerocoin = GetZerocoinSeed(i);
    libzerocoin::PrivateCoin MintedCoin(libzerocoin::gpZerocoinParams);
    CBigNum bnValue = MintedCoin.CoinFromSeed(seedZerocoin);

    mintPool.Add(bnValue, i);
    gWalletDB.WriteMintPoolPair(hashSeed, GetPubCoinHash(bnValue), i);
    int64_t now = GetTimeMillis();
    LogPrint(ClubLog::ZERO, "%s : %s count=%d, time total= %d (ms), this coin time = %d (ms)\n", __func__,
             bnValue.GetHex().substr(0, 6), i, now - nTime_ref, now - nTime_delta);
  }
}

// pubcoin hashes are stored to db so that a full accounting of mints belonging to the seed can be tracked without
// regenerating
bool CZeroWallet::LoadMintPoolFromDB() {
  map<uint256, vector<pair<uint256, uint32_t> > > mapMintPool = gWalletDB.MapMintPool();

  uint256 hashSeed = Hash(seedMaster.begin(), seedMaster.end());
  for (auto& pair : mapMintPool[hashSeed]) mintPool.Add(pair);

  return true;
}

void CZeroWallet::RemoveMintsFromPool(const std::vector<uint256>& vPubcoinHashes) {
  for (const uint256& hash : vPubcoinHashes) mintPool.Remove(hash);
}

void CZeroWallet::GetState(int& nCount, int& nLastGenerated) {
  nCount = this->nCountLastUsed + 1;
  nLastGenerated = mintPool.CountOfLastGenerated();
}

// Catch the counter up with the chain
void CZeroWallet::SyncWithChain(bool fGenerateMintPool) {
  uint32_t nLastCountUsed = 0;
  bool found = true;

  set<uint256> setAddedTx;
  while (found) {
    found = false;
    if (fGenerateMintPool) GenerateZMintPool();
    LogPrint(ClubLog::ZERO, "%s: Mintpool size=%d\n", __func__, mintPool.size());

    std::set<uint256> setChecked;
    list<pair<uint256, uint32_t> > listMints = mintPool.List();
    for (pair<uint256, uint32_t> pMint : listMints) {
      LOCK(cs_main);
      if (setChecked.count(pMint.first)) return;
      setChecked.insert(pMint.first);

      if (ShutdownRequested()) return;

      if (pwalletMain->zkpTracker->HasPubcoinHash(pMint.first)) {
        mintPool.Remove(pMint.first);
        continue;
      }

      uint256 txHash;
      CZerocoinMint mint;
      if (zerocoinDB->ReadCoinMint(pMint.first, txHash)) {
        // this mint has already occurred on the chain, increment counter's state to reflect this
        LogPrint(ClubLog::ZERO, "%s : Found wallet coin mint=%s count=%d tx=%s\n", __func__, pMint.first.GetHex(),
                 pMint.second, txHash.GetHex());
        found = true;

        uint256 hashBlock;
        CTransaction tx;
        if (!GetTransaction(txHash, tx, hashBlock, true)) {
          LogPrintf("%s : failed to get transaction for mint %s!\n", __func__, pMint.first.GetHex());
          found = false;
          nLastCountUsed = std::max(pMint.second, nLastCountUsed);
          continue;
        }

        // Find the denomination
        libzerocoin::CoinDenomination denomination = libzerocoin::ZQ_ERROR;
        bool fFoundMint = false;
        CBigNum bnValue = 0;
        for (const CTxOut& out : tx.vout) {
          if (!out.scriptPubKey.IsZerocoinMint()) continue;

          libzerocoin::PublicCoin pubcoin;
          CValidationState state;
          if (!TxOutToPublicCoin(out, pubcoin, state)) {
            LogPrintf("%s : failed to get mint from txout for %s!\n", __func__, pMint.first.GetHex());
            continue;
          }

          // See if this is the mint that we are looking for
          uint256 hashPubcoin = GetPubCoinHash(pubcoin.getValue());
          if (pMint.first == hashPubcoin) {
            denomination = pubcoin.getDenomination();
            bnValue = pubcoin.getValue();
            fFoundMint = true;
            break;
          }
        }

        if (!fFoundMint || denomination == libzerocoin::ZQ_ERROR) {
          LogPrintf("%s : failed to get mint %s from tx %s!\n", __func__, pMint.first.GetHex(), tx.GetHash().GetHex());
          found = false;
          break;
        }

        CBlockIndex* pindex = nullptr;
        if (mapBlockIndex.count(hashBlock)) pindex = mapBlockIndex.at(hashBlock);

        if (!setAddedTx.count(txHash)) {
          CBlock block;
          CWalletTx wtx(pwalletMain, tx);
          if (pindex && ReadBlockFromDisk(block, pindex)) wtx.SetMerkleBranch(block);

          // Fill out wtx so that a transaction record can be created
          wtx.nTimeReceived = pindex->GetBlockTime();
          pwalletMain->AddToWallet(wtx);
          setAddedTx.insert(txHash);
        }

        SetMintSeen(bnValue, pindex->nHeight, txHash, denomination);
        nLastCountUsed = std::max(pMint.second, nLastCountUsed);
        nCountLastUsed = std::max(nLastCountUsed, nCountLastUsed);
        LogPrint(ClubLog::ZERO, "%s: updated count to %d\n", __func__, nCountLastUsed);
      }
    }
  }
}

bool CZeroWallet::SetMintSeen(const CBigNum& bnValue, const int& nHeight, const uint256& txid,
                              const libzerocoin::CoinDenomination& denom) {
  if (!mintPool.Has(bnValue)) return error("%s: value not in pool", __func__);
  pair<uint256, uint32_t> pMint = mintPool.Get(bnValue);

  // Regenerate the mint
  uint512 seedZerocoin = GetZerocoinSeed(pMint.second);
  libzerocoin::PrivateCoin MintedCoin(libzerocoin::gpZerocoinParams);
  CBigNum bnValueGen = MintedCoin.CoinFromSeed(seedZerocoin);

  // Sanity check
  if (bnValueGen != bnValue) return error("%s: generated pubcoin and expected value do not match!", __func__);

  // Create mint object and database it
  uint256 hashSeed = Hash(seedMaster.begin(), seedMaster.end());
  uint256 hashSerial = GetSerialHash(MintedCoin.getSerialNumber());
  uint256 hashPubcoin = GetPubCoinHash(bnValue);
  uint256 nSerial = MintedCoin.getSerialNumber().getuint256();
  uint256 hashStake = Hash(nSerial.begin(), nSerial.end());
  CDeterministicMint dMint(libzerocoin::PrivateCoin::PRIVATECOIN_VERSION, pMint.second, hashSeed, hashSerial,
                           hashPubcoin, hashStake);
  dMint.SetDenomination(denom);
  dMint.SetHeight(nHeight);
  dMint.SetTxHash(txid);

  // Check if this is also already spent
  int nHeightTx;
  uint256 txidSpend;
  CTransaction txSpend;
  if (IsSerialInBlockchain(hashSerial, nHeightTx, txidSpend, txSpend)) {
    // Find transaction details and make a wallettx and add to wallet
    dMint.SetUsed(true);
    CWalletTx wtx(pwalletMain, txSpend);
    CBlockIndex* pindex = chainActive[nHeightTx];
    CBlock block;
    if (ReadBlockFromDisk(block, pindex)) wtx.SetMerkleBranch(block);

    wtx.nTimeReceived = pindex->nTime;
    pwalletMain->AddToWallet(wtx);
  }

  // Add to zkpTracker which also adds to database
  pwalletMain->zkpTracker->Add(dMint, true);

  // Update the count if it is less than the mint's count
  if (nCountLastUsed < pMint.second) {
    nCountLastUsed = pMint.second;
    gWalletDB.WriteZKPCount(nCountLastUsed);
  }

  // remove from the pool
  mintPool.Remove(dMint.GetPubcoinHash());

  return true;
}

uint512 CZeroWallet::GetZerocoinSeed(uint32_t n) {
  CDataStream ss(SER_GETHASH);
  ss << seedMaster << n;
  uint512 zerocoinSeed = Hash512(ss.begin(), ss.end());
  return zerocoinSeed;
}

void CZeroWallet::UpdateCount() {
  nCountLastUsed++;
  gWalletDB.WriteZKPCount(nCountLastUsed);
}

void CZeroWallet::GenerateDeterministicZKP(libzerocoin::CoinDenomination denom, libzerocoin::PrivateCoin& coin,
                                           CDeterministicMint& dMint, bool fGenerateOnly) {
  GenerateMint(nCountLastUsed + 1, denom, coin, dMint);
  if (fGenerateOnly) return;

  // TODO remove this leak of seed from logs before merge to master
  // LogPrintf("%s : Generated new deterministic mint. Count=%d pubcoin=%s seed=%s\n", __func__, nCount,
  // coin.getPublicCoin().getValue().GetHex().substr(0,6), seedZerocoin.GetHex().substr(0, 4));
}

void CZeroWallet::GenerateMint(const uint32_t& nCount, const libzerocoin::CoinDenomination denom,
                               libzerocoin::PrivateCoin& coin, CDeterministicMint& dMint) {
  uint512 seedZerocoin = GetZerocoinSeed(nCount);
  CBigNum bnValue = coin.CoinFromSeed(seedZerocoin);
  coin.setVersion(libzerocoin::PrivateCoin::PRIVATECOIN_VERSION);

  uint256 hashSeed = Hash(seedMaster.begin(), seedMaster.end());
  uint256 hashSerial = GetSerialHash(coin.getSerialNumber());
  uint256 nSerial = coin.getSerialNumber().getuint256();
  uint256 hashStake = Hash(nSerial.begin(), nSerial.end());
  uint256 hashPubcoin = GetPubCoinHash(bnValue);
  dMint = CDeterministicMint(coin.getVersion(), nCount, hashSeed, hashSerial, hashPubcoin, hashStake);
  dMint.SetDenomination(denom);
}

bool CZeroWallet::RegenerateMint(const CDeterministicMint& dMint, CZerocoinMint& mint) {
  // Check that the seed is correct    todo:handling of incorrect, or multiple seeds
  uint256 hashSeed = Hash(seedMaster.begin(), seedMaster.end());
  if (hashSeed != dMint.GetSeedHash())
    return error("%s: master seed does not match!\ndmint:\n %s \nhashSeed: %s\nseed: %s", __func__, dMint.ToString(),
                 hashSeed.GetHex(), seedMaster.GetHex());

  // Generate the coin
  libzerocoin::PrivateCoin coin(libzerocoin::gpZerocoinParams);
  CDeterministicMint dMintDummy;
  GenerateMint(dMint.GetCount(), dMint.GetDenomination(), coin, dMintDummy);

  // Fill in the zerocoinmint object's details
  CBigNum bnValue = coin.getPublicCoin().getValue();
  if (GetPubCoinHash(bnValue) != dMint.GetPubcoinHash())
    return error("%s: failed to correctly generate mint, pubcoin hash mismatch", __func__);
  mint.SetValue(bnValue);

  CBigNum bnSerial = coin.getSerialNumber();
  if (GetSerialHash(bnSerial) != dMint.GetSerialHash())
    return error("%s: failed to correctly generate mint, serial hash mismatch", __func__);
  mint.SetSerialNumber(bnSerial);

  mint.SetRandomness(coin.getRandomness());
  mint.SetPrivKey(coin.getPrivKey());
  mint.SetVersion(coin.getVersion());
  mint.SetDenomination(dMint.GetDenomination());
  mint.SetUsed(dMint.IsUsed());
  mint.SetTxHash(dMint.GetTxHash());
  mint.SetHeight(dMint.GetHeight());

  return true;
}
