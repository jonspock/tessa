// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "miner.h"
#include "amount.h"
#include "bls/key.h"
#include "hash.h"
#include "init.h"
#include "kernel.h"  // mapHashedBlocks
#include "main.h"    // for CBlockTemplate, updateMapZerocoinSpends
#include "net.h"
#include "pow.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "staker.h"
#include "timedata.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utiltime.h"
#include "wallet/wallet.h"
#include "wallet/wallettx.h"

#include "bls/blocksignature.h"
#include "validationinterface.h"
#include "validationstate.h"
#include "zerocoin/accumulators.h"

#include "libzerocoin/CoinSpend.h"
#include <cmath>  // for std::pow
#include <thread>

using namespace std;
using namespace bls;

static std::condition_variable miner_interrupt_cond;
static std::mutex cs_miner_interrupt;
static std::atomic<bool> miner_interrupted(false);

static void InterruptibleSleep(uint64_t n) {
  bool ret = false;
  {
    std::unique_lock<std::mutex> lock(cs_miner_interrupt);
    ret = miner_interrupt_cond.wait_for(lock, std::chrono::milliseconds(n), []() -> bool { return miner_interrupted; });
  }
  interruption_point(ret);
}

//////////////////////////////////////////////////////////////////////////////
//
// TessaMiner
//

//
// Unconfirmed transactions in the memory pool often depend on other
// transactions in the memory pool. When we select transactions from the
// pool, we select by highest priority or fee rate, so we might consider
// transactions that depend on transactions that aren't yet in the block.
// The COrphan class keeps track of these 'temporary orphans' while
// CreateBlock is figuring out which transactions to include.
//
class COrphan {
 public:
  const CTransaction* ptx;
  set<uint256> setDependsOn;
  CAmount feeRate;
  double dPriority;

  COrphan(const CTransaction* ptxIn) : ptx(ptxIn), feeRate(0), dPriority(0) {}
};

static uint64_t nLastBlockTx = 0;
static uint64_t nLastBlockSize = 0;

uint64_t getLastBlockTx() { return nLastBlockTx; }
uint64_t getLastBlockSize() { return nLastBlockSize; }

// We want to sort transactions by priority and fee rate, so:
typedef std::tuple<double, CAmount, const CTransaction*> TxPriority;
class TxPriorityCompare {
  bool byFee;

 public:
  TxPriorityCompare(bool _byFee) : byFee(_byFee) {}

  bool operator()(const TxPriority& a, const TxPriority& b) {
    if (byFee) {
      if (get<1>(a) == get<1>(b)) return get<0>(a) < get<0>(b);
      return get<1>(a) < get<1>(b);
    } else {
      if (get<0>(a) == get<0>(b)) return get<1>(a) < get<1>(b);
      return get<0>(a) < get<0>(a);
    }
  }
};

void UpdateTime(CBlockHeader* pblock, const CBlockIndex* pindexPrev) {
  pblock->nTime = std::max(pindexPrev->GetMedianTimePast() + 1, GetAdjustedTime());

  // Updating time can change work required on testnet:
  if (Params().AllowMinDifficultyBlocks()) pblock->nBits = GetNextWorkRequired(pindexPrev, pblock);
}

std::pair<int, std::pair<uint256, uint256> > pCheckpointCache;
CBlockTemplate* CreateNewBlock(const CScript& scriptPubKeyIn, CWallet* pwallet, bool fProofOfStake) {
  CReserveKey reservekey(pwallet);

  // Create new block
  unique_ptr<CBlockTemplate> pblocktemplate(new CBlockTemplate());
  if (!pblocktemplate.get()) return nullptr;
  CBlock* pblock = &pblocktemplate->block;  // pointer for convenience

  // -regtest only: allow overriding block.nVersion with
  // -blockversion=N to test forking scenarios
  if (Params().MineBlocksOnDemand()) pblock->nHeaderVersion = GetArg("-blockversion", pblock->nHeaderVersion);

    // Check if zerocoin is enabled
    // XXXX warning "Check zerocoin start here too""
#ifndef ZEROCOIN_DISABLED
  bool fZerocoinActive = (chainActive.Tip()->nHeight) >= Params().Zerocoin_StartHeight();
  fZerocoinActive = true;
#endif

  // Create coinbase tx
  CMutableTransaction txNew;
  txNew.vin.resize(1);
  txNew.vin[0].prevout.SetNull();
  txNew.vout.resize(1);
  txNew.vout[0].scriptPubKey = scriptPubKeyIn;
  pblock->vtx.push_back(txNew);
  pblocktemplate->vTxFees.push_back(-1);    // updated at end
  pblocktemplate->vTxSigOps.push_back(-1);  // updated at end

  // ppcoin: if coinstake available add coinstake tx
  gStaker.Setup(GetAdjustedTime());  // only initialized at startup

  if (fProofOfStake) {
    interruption_point(miner_interrupted);
    bool fStakeFound = gStaker.FindStake(GetAdjustedTime(), chainActive.Tip(), pblock, pwallet);
    if (!fStakeFound) return nullptr;
  }

  // Largest block you're willing to create:
  uint32_t nBlockMaxSize = GetArg("-blockmaxsize", DEFAULT_BLOCK_MAX_SIZE);
  // Limit to betweeen 1K and MAX_BLOCK_SIZE-1K for sanity:
  uint32_t nBlockMaxSizeNetwork = MAX_BLOCK_SIZE_CURRENT;
  nBlockMaxSize = std::max((uint32_t)1000, std::min((nBlockMaxSizeNetwork - 1000), nBlockMaxSize));

  // How much of the block should be dedicated to high-priority transactions,
  // included regardless of the fees they pay
  uint32_t nBlockPrioritySize = GetArg("-blockprioritysize", DEFAULT_BLOCK_PRIORITY_SIZE);
  nBlockPrioritySize = std::min(nBlockMaxSize, nBlockPrioritySize);

  // Minimum block size you want to create; block will be filled with free transactions
  // until there are no more or the block reaches this size:
  uint32_t nBlockMinSize = GetArg("-blockminsize", DEFAULT_BLOCK_MIN_SIZE);
  nBlockMinSize = std::min(nBlockMaxSize, nBlockMinSize);

  // Collect memory pool transactions into the block
  CAmount nFees = 0;

  {
    LOCK2(cs_main, mempool.cs);

    CBlockIndex* pindexPrev = chainActive.Tip();
    const int nHeight = pindexPrev->nHeight + 1;
    CCoinsViewCache view(gpCoinsTip);

    // Priority order to process transactions
    list<COrphan> vOrphan;  // list memory doesn't move
    map<uint256, vector<COrphan*> > mapDependers;
    bool fPrintPriority = GetBoolArg("-printpriority", false);

    // This vector will be sorted into a priority queue:
    vector<TxPriority> vecPriority;
    vecPriority.reserve(mempool.mapTx.size());
    for (auto& mi : mempool.mapTx) {
      const CTransaction& tx = mi.second.GetTx();
      if (tx.IsCoinBase() || tx.IsCoinStake() || !IsFinalTx(tx, nHeight)) { continue; }

      COrphan* porphan = nullptr;
      double dPriority = 0;
      CAmount nTotalIn = 0;
      bool fMissingInputs = false;
      uint256 txid = tx.GetHash();
      for (const CTxIn& txin : tx.vin) {
        // zerocoinspend has special vin
        if (tx.IsZerocoinSpend()) {
#ifndef ZEROCOIN_DISABLED
          nTotalIn = tx.GetZerocoinSpent();

          // Give a high priority to zerocoinspends to get into the next block
          // Priority = (age^6+100000)*amount - gives higher priority to zkps that have been in mempool long
          // and higher priority to zkps that are large in value
          int64_t nTimeSeen = GetAdjustedTime();
          double nConfs = 100000;

          // update MapZerocoinSpends if not seen, otherwise return nTimeSeen
          updateMapZerocoinSpends(txid, nTimeSeen);

          double nTimePriority = std::pow(GetAdjustedTime() - nTimeSeen, 6);

          // ZKP spends can have very large priority, use non-overflowing safe functions
          dPriority = double_safe_addition(dPriority, (nTimePriority * nConfs));
          dPriority = double_safe_multiplication(dPriority, nTotalIn);
#endif
          continue;
        }

        // Read prev transaction
        if (!view.HaveCoins(txin.prevout.hash)) {
          // This should never happen; all transactions in the memory
          // pool should connect to either transactions in the chain
          // or other transactions in the memory pool.
          if (!mempool.mapTx.count(txin.prevout.hash)) {
            LogPrintf("ERROR: mempool transaction missing input\n");
            if (gArgs.IsArgSet("-debug")) assert("mempool transaction missing input" == 0);
            fMissingInputs = true;
            if (porphan) vOrphan.pop_back();
            break;
          }

          // Has to wait for dependencies
          if (!porphan) {
            // Use list for automatic deletion
            vOrphan.push_back(COrphan(&tx));
            porphan = &vOrphan.back();
          }
          mapDependers[txin.prevout.hash].push_back(porphan);
          porphan->setDependsOn.insert(txin.prevout.hash);
          nTotalIn += mempool.mapTx[txin.prevout.hash].GetTx().vout[txin.prevout.n].nValue;
          continue;
        }

        const CCoins* coins = view.AccessCoins(txin.prevout.hash);
        assert(coins);

        CAmount nValueIn = coins->vout[txin.prevout.n].nValue;
        nTotalIn += nValueIn;

        int nConf = nHeight - coins->nHeight;

        // ZKP spends can have very large priority, use non-overflowing safe functions
        dPriority = double_safe_addition(dPriority, ((double)nValueIn * nConf));
      }
      if (fMissingInputs) continue;

      // Priority is sum(valuein * age) / modified_txsize
      uint32_t nTxSize = ::GetSerializeSize(tx);
      dPriority = tx.ComputePriority(dPriority, nTxSize);

      uint256 hash = tx.GetHash();
      mempool.ApplyDeltas(hash, dPriority, nTotalIn);

      if (porphan) {
        porphan->dPriority = dPriority;
        porphan->feeRate = minTxFee;
      } else
        vecPriority.push_back(TxPriority(dPriority, minTxFee, &mi.second.GetTx()));
    }

    // Collect transactions into block
    uint64_t nBlockSize = 1000;
    uint64_t nBlockTx = 0;
    int nBlockSigOps = 100;
    bool fSortedByFee = (nBlockPrioritySize <= 0);

    TxPriorityCompare comparer(fSortedByFee);
    std::make_heap(vecPriority.begin(), vecPriority.end(), comparer);

    vector<CBigNum> vBlockSerials;
    vector<CBigNum> vTxSerials;
    while (!vecPriority.empty()) {
      // Take highest priority transaction off the priority queue:
      double dPriority = get<0>(vecPriority.front());
      CAmount feeRate = get<1>(vecPriority.front());
      const CTransaction& tx = *(get<2>(vecPriority.front()));

      std::pop_heap(vecPriority.begin(), vecPriority.end(), comparer);
      vecPriority.pop_back();

      // Size limits
      uint32_t nTxSize = ::GetSerializeSize(tx);
      if (nBlockSize + nTxSize >= nBlockMaxSize) continue;

      // Legacy limits on sigOps:
      uint32_t nMaxBlockSigOps = MAX_BLOCK_SIGOPS_CURRENT;
      uint32_t nTxSigOps = GetLegacySigOpCount(tx);
      if (nBlockSigOps + nTxSigOps >= nMaxBlockSigOps) continue;

      // Skip free transactions if we're past the minimum block size:
      const uint256& hash = tx.GetHash();
      double dPriorityDelta = 0;
      CAmount nFeeDelta = 0;
      mempool.ApplyDeltas(hash, dPriorityDelta, nFeeDelta);
      if (!tx.IsZerocoinSpend() && fSortedByFee && (dPriorityDelta <= 0) && (nFeeDelta <= 0) &&
          (feeRate < minRelayTxFee) && (nBlockSize + nTxSize >= nBlockMinSize))
        continue;

      // Prioritise by fee once past the priority size or we run out of high-priority
      // transactions:
      if (!fSortedByFee && ((nBlockSize + nTxSize >= nBlockPrioritySize) || !AllowFree(dPriority))) {
        fSortedByFee = true;
        comparer = TxPriorityCompare(fSortedByFee);
        std::make_heap(vecPriority.begin(), vecPriority.end(), comparer);
      }

      if (!view.HaveInputs(tx)) continue;

      // double check that there are no double spent ZKP spends in this block or tx
      if (tx.IsZerocoinSpend()) {
#ifndef ZEROCOIN_DISABLED
        int nHeightTx = 0;
        if (IsTransactionInChain(tx.GetHash(), nHeightTx)) continue;

        bool fDoubleSerial = false;
        for (const CTxIn& txIn : tx.vin) {
          if (txIn.scriptSig.IsZerocoinSpend()) {
            libzerocoin::CoinSpend spend = TxInToZerocoinSpend(txIn);
            if (!spend.HasValidSerial(libzerocoin::gpZerocoinParams)) fDoubleSerial = true;
            if (count(vBlockSerials.begin(), vBlockSerials.end(), spend.getCoinSerialNumber())) fDoubleSerial = true;
            if (count(vTxSerials.begin(), vTxSerials.end(), spend.getCoinSerialNumber())) fDoubleSerial = true;
            if (fDoubleSerial) break;
            vTxSerials.emplace_back(spend.getCoinSerialNumber());
          }
        }
        // This ZKP serial has already been included in the block, do not add this tx.
        if (fDoubleSerial) continue;
#endif
      }

      CAmount nTxFees = view.GetValueIn(tx) - tx.GetValueOut();

      nTxSigOps += GetP2SHSigOpCount(tx, view);
      if (nBlockSigOps + nTxSigOps >= nMaxBlockSigOps) continue;

      // Note that flags: we don't want to set mempool/IsStandard()
      // policy here, but we still have to ensure that the block we
      // create only contains transactions that are valid in new blocks.
      CValidationState state;
      if (!CheckInputs(tx, state, view, true, MANDATORY_SCRIPT_VERIFY_FLAGS, true)) continue;

      CTxUndo txundo;
      UpdateCoins(tx, state, view, txundo, nHeight);

      // Added
      pblock->vtx.push_back(tx);
      pblocktemplate->vTxFees.push_back(nTxFees);
      pblocktemplate->vTxSigOps.push_back(nTxSigOps);
      nBlockSize += nTxSize;
      ++nBlockTx;
      nBlockSigOps += nTxSigOps;
      nFees += nTxFees;

      for (const CBigNum& bnSerial : vTxSerials) vBlockSerials.emplace_back(bnSerial);

      if (fPrintPriority) {
        LogPrint(TessaLog::MINER, "priority %.1f fee %d txid %s\n", dPriority, feeRate, tx.GetHash().ToString());
      }

      // Add transactions that depend on this one to the priority queue
      if (mapDependers.count(hash)) {
        for (COrphan* porphan : mapDependers[hash]) {
          if (!porphan->setDependsOn.empty()) {
            porphan->setDependsOn.erase(hash);
            if (porphan->setDependsOn.empty()) {
              vecPriority.push_back(TxPriority(porphan->dPriority, porphan->feeRate, porphan->ptx));
              std::push_heap(vecPriority.begin(), vecPriority.end(), comparer);
            }
          }
        }
      }
    }

    if (fProofOfStake) {
      pblock->vtx[0].vin[0].scriptSig = CScript() << nHeight << OP_0;
      // Fill in header
      pblock->hashPrevBlock = pindexPrev->GetBlockHash();
    } else {
      // Make payee
      if (txNew.vout.size() > 1) {
        pblock->payee = txNew.vout[1].scriptPubKey;
      } else {
        // XXX Check later but seems needed
        CAmount blockValue = GetBlockValue(pindexPrev->nHeight);
        txNew.vout[0].nValue = blockValue;
      }

      pblock->vtx[0] = txNew;
      pblock->vtx[0].vin[0].scriptSig = CScript() << nHeight << OP_0;
      // Compute final coinbase transaction.
      pblocktemplate->vTxFees[0] = -nFees;
      // Fill in header
      pblock->hashPrevBlock = pindexPrev->GetBlockHash();
      UpdateTime(pblock, pindexPrev);
    }

    pblock->nBits = GetNextWorkRequired(pindexPrev, pblock);
    pblock->nNonce = 0;

    nLastBlockTx = nBlockTx;
    nLastBlockSize = nBlockSize;
    LogPrint(TessaLog::MINER, "CreateNewBlock(): total size %u\n", nBlockSize);

    // Calculate the accumulator checkpoint only if the previous cached checkpoint need to be updated
#ifndef ZEROCOIN_DISABLED
    uint256 nCheckpoint;
    if (nHeight > ACC_BLOCK_INTERVAL) {
      uint256 hashBlockLastAccumulated =
          chainActive[nHeight - (nHeight % ACC_BLOCK_INTERVAL) - ACC_BLOCK_INTERVAL]->GetBlockHash();
      if (nHeight >= pCheckpointCache.first || pCheckpointCache.second.first != hashBlockLastAccumulated) {
        // For the period before v2 activation, ZKP will be disabled and previous block's checkpoint is all that will
        // be needed
        pCheckpointCache.second.second = pindexPrev->nAccumulatorCheckpoint;
        if (pindexPrev->nHeight + 1 >= Params().Zerocoin_StartHeight()) {
          AccumulatorMap mapAccumulators(libzerocoin::gpZerocoinParams);
          if (fZerocoinActive && !CalculateAccumulatorCheckpoint(nHeight, nCheckpoint, mapAccumulators)) {
            LogPrintf("MINER %s: failed to get accumulator checkpoint\n", __func__);
          } else {
            // the next time the accumulator checkpoint should be recalculated ( the next height that is multiple of
            // ACC_BLOCK_INTERVAL)
            pCheckpointCache.first = nHeight + (ACC_BLOCK_INTERVAL - (nHeight % ACC_BLOCK_INTERVAL));

            // the block hash of the last block used in the accumulator checkpoint calc. This will handle reorg
            // situations.
            pCheckpointCache.second.first = hashBlockLastAccumulated;
            pCheckpointCache.second.second = nCheckpoint;
          }
        }
      }

      pblock->nAccumulatorCheckpoint = pCheckpointCache.second.second;
    }
#endif
    pblocktemplate->vTxSigOps[0] = GetLegacySigOpCount(pblock->vtx[0]);

    CValidationState state;
    if (!TestBlockValidity(state, *pblock, pindexPrev, false, false)) {
      LogPrintf("MINER CreateNewBlock() : TestBlockValidity failed\n");
      mempool.clear();
      return nullptr;
    }

    //        if (pblock->IsZerocoinStake()) {
    //            CWalletTx wtx(pwalletMain, pblock->vtx[1]);
    //            pwalletMain->AddToWallet(wtx);
    //        }
  }

  return pblocktemplate.release();
}

void IncrementExtraNonce(CBlock* pblock, CBlockIndex* pindexPrev, uint32_t& nExtraNonce) {
  // Update nExtraNonce
  static uint256 hashPrevBlock;
  if (hashPrevBlock != pblock->hashPrevBlock) {
    nExtraNonce = 0;
    hashPrevBlock = pblock->hashPrevBlock;
  }
  ++nExtraNonce;
  uint32_t nHeight = pindexPrev->nHeight + 1;  // Height first in coinbase required for block.version=2
  CMutableTransaction txCoinbase(pblock->vtx[0]);
  txCoinbase.vin[0].scriptSig = (CScript() << nHeight << CScriptNum(nExtraNonce)) + COINBASE_FLAGS;
  assert(txCoinbase.vin[0].scriptSig.size() <= 100);

  pblock->vtx[0] = txCoinbase;
  pblock->hashMerkleRoot = pblock->BuildMerkleTree();
}

//////////////////////////////////////////////////////////////////////////////
//
// Internal miner
//
double dHashesPerSec = 0.0;
int64_t nHPSTimerStart = 0;

CBlockTemplate* CreateNewBlockWithKey(CReserveKey& reservekey, CWallet* pwallet, bool fProofOfStake) {
  CPubKey pubkey;
  if (!reservekey.GetReservedKey(pubkey)) return nullptr;

  CScript scriptPubKey = CScript() << pubkey.ToStdVector() << OP_CHECKSIG;
  return CreateNewBlock(scriptPubKey, pwallet, fProofOfStake);
}

bool ProcessBlockFound(CBlock* pblock, CWallet& wallet, CReserveKey& reservekey) {
  LogPrint(TessaLog::MINER, "generated %s\n", FormatMoney(pblock->vtx[0].vout[0].nValue));

  // Found a solution
  {
    LOCK(cs_main);
    if (pblock->hashPrevBlock != chainActive.Tip()->GetBlockHash())
      return error("TessaMiner : generated block is stale");
  }

  // Remove key from key pool
  reservekey.KeepKey();

  // Track how many getdata requests this block gets
  {
    LOCK(wallet.cs_wallet);
    wallet.mapRequestCount[pblock->GetHash()] = 0;
  }

  // Inform about the new block
  GetMainSignals().BlockFound.fire(pblock->GetHash());

  // Process this block the same as if we had received it from another node
  CValidationState state;
  if (!ProcessNewBlock(state, nullptr, pblock)) {
    // if (pblock->IsZerocoinStake()) pwalletMain->zkpTracker->RemovePending(pblock->vtx[1].GetHash());
    return error("TessaMiner : ProcessNewBlock, block not accepted");
  }

  for (CNode* node : vNodes) { node->PushInventory(CInv(MSG_BLOCK, pblock->GetHash())); }

  return true;
}

bool fGenerateBitcoins = false;
bool fMintableCoins = false;
int nMintableLastCheck = 0;

// ***TODO*** that part changed in bitcoin, we are using a mix with old one here for now

void BitcoinMiner(CWallet* pwallet, bool fProofOfStake) {
  LogPrintf("TessaMiner started\n");
  SetThreadPriority(THREAD_PRIORITY_LOWEST);
  RenameThread("tessa-miner");

  // Each thread has its own key and counter
  CReserveKey reservekey(pwallet);
  uint32_t nExtraNonce = 0;

  while (fGenerateBitcoins || fProofOfStake) {
    if (fProofOfStake) {
      // control the amount of times the client will check for mintable coins
      if ((GetTime() - nMintableLastCheck > 5 * 60))  // 5 minute check time
      {
        nMintableLastCheck = GetTime();
        fMintableCoins = pwallet->MintableCoins();
      }

      if (chainActive.Tip()->nHeight < Params().LAST_POW_BLOCK()) {
        InterruptibleSleep(5000);
        continue;
      }

      // while (vNodes.empty() || pwallet->IsLocked() || !fMintableCoins || (pwallet->GetBalance() > 0 &&
      // nReserveBalance >= pwallet->GetBalance())) {

      while (pwallet->IsLocked() || !fMintableCoins ||
             (pwallet->GetBalance() > 0 && getReserveBalance() >= pwallet->GetBalance())) {
        gStaker.setLastCoinStakeSearchInterval(0);
        // Do a separate 1 minute check here to ensure fMintableCoins is updated
        if (!fMintableCoins) {
          if (GetTime() - nMintableLastCheck > 1 * 60)  // 1 minute check time
          {
            nMintableLastCheck = GetTime();
            fMintableCoins = pwallet->MintableCoins();
            std::cout << "Rechecking if Mintable : " << fMintableCoins << " " << GetTime() << "\n";
          }
        }
        InterruptibleSleep(5000);
        if (!fGenerateBitcoins && !fProofOfStake) continue;
      }

      if (mapHashedBlocks.count(
              chainActive.Tip()->nHeight))  // search our map of hashed blocks, see if bestblock has been hashed yet
      {
        if (GetTime() - mapHashedBlocks[chainActive.Tip()->nHeight] <
            max(pwallet->nHashInterval, (uint32_t)1))  // wait half of the nHashDrift with max wait of 3 minutes
        {
          InterruptibleSleep(5000);
          continue;
        }
      }
    }

    // If Mining PoW and reach the End of the Period Automatically Switch to Proof-of-Stake
    if (chainActive.Tip()->nHeight >= Params().LAST_POW_BLOCK()) fProofOfStake = true;

    //
    // Create new block
    //
    uint32_t nTransactionsUpdatedLast = mempool.GetTransactionsUpdated();
    CBlockIndex* pindexPrev = chainActive.Tip();
    if (!pindexPrev) continue;

    unique_ptr<CBlockTemplate> pblocktemplate(CreateNewBlockWithKey(reservekey, pwallet, fProofOfStake));
    if (!pblocktemplate.get()) continue;

    CBlock* pblock = &pblocktemplate->block;
    IncrementExtraNonce(pblock, pindexPrev, nExtraNonce);

    // Stake miner main
    if (fProofOfStake) {
      LogPrint(TessaLog::MINER, " : proof-of-stake block found %s \n", pblock->GetHash().ToString().c_str());
      if (!SignBlock(*pblock, *pwallet)) {
        LogPrintf("BitcoinMiner(): Signing new block with UTXO key failed \n");
        continue;
      }

      LogPrint(TessaLog::MINER, "CPUMiner : proof-of-stake block was signed %s \n",
               pblock->GetHash().ToString().c_str());
      SetThreadPriority(THREAD_PRIORITY_NORMAL);
      ProcessBlockFound(pblock, *pwallet, reservekey);
      SetThreadPriority(THREAD_PRIORITY_LOWEST);

      continue;
    }

    LogPrint(TessaLog::MINER, "Running TessaMiner with %u transactions in block (%u bytes)\n", pblock->vtx.size(),
             ::GetSerializeSize(*pblock));

    //
    // Search
    //
    int64_t nStart = GetTime();
    arith_uint256 hashTarget = arith_uint256().SetCompact(pblock->nBits);
    while (true) {
      uint32_t nHashesDone = 0;

      uint256 hash;
      while (true) {
        hash = pblock->GetHash();
        if (UintToArith256(hash) <= hashTarget) {
          // Found a solution
          SetThreadPriority(THREAD_PRIORITY_NORMAL);
          LogPrint(TessaLog::MINER, "proof-of-work found : hash: %s  : target: %s\n", hash.GetHex(),
                   hashTarget.GetHex());
          ProcessBlockFound(pblock, *pwallet, reservekey);
          SetThreadPriority(THREAD_PRIORITY_LOWEST);

          // In regression test mode, stop mining after a block is found. This
          // allows developers to controllably generate a block on demand.
          if (Params().MineBlocksOnDemand()) {
            miner_interrupted = true;
            interruption_point(miner_interrupted);
          }

          break;
        }
        pblock->nNonce += 1;
        nHashesDone += 1;
        if ((pblock->nNonce & 0xFF) == 0) break;
      }

      // Meter hashes/sec
      static int64_t nHashCounter;
      if (nHPSTimerStart == 0) {
        nHPSTimerStart = GetTimeMillis();
        nHashCounter = 0;
      } else
        nHashCounter += nHashesDone;
      if (GetTimeMillis() - nHPSTimerStart > 4000) {
        static CCriticalSection cs;
        {
          LOCK(cs);
          if (GetTimeMillis() - nHPSTimerStart > 4000) {
            dHashesPerSec = 1000.0 * nHashCounter / (GetTimeMillis() - nHPSTimerStart);
            nHPSTimerStart = GetTimeMillis();
            nHashCounter = 0;
            static int64_t nLogTime;
            if (GetTime() - nLogTime > 30 * 60) {
              nLogTime = GetTime();
              LogPrint(TessaLog::MINER, "hashmeter %6.0f khash/s\n", dHashesPerSec / 1000.0);
            }
          }
        }
      }

      // Check for stop or if block needs to be rebuilt
      interruption_point(miner_interrupted);
      // Regtest mode doesn't require peers
      if (vNodes.empty() && Params().MiningRequiresPeers()) break;
      if (pblock->nNonce >= 0xffff0000) break;
      if (mempool.GetTransactionsUpdated() != nTransactionsUpdatedLast && GetTime() - nStart > 60) break;
      if (pindexPrev != chainActive.Tip()) break;

      // Update nTime every few seconds
      UpdateTime(pblock, pindexPrev);
      if (Params().AllowMinDifficultyBlocks()) {
        // Changing pblock->nTime can change work required on testnet:
        hashTarget.SetCompact(pblock->nBits);
      }
    }
  }
}
void InterruptMiner() {
  miner_interrupted = true;
  miner_interrupt_cond.notify_all();
}

void static ThreadBitcoinMiner(void* parg) {
  interruption_point(miner_interrupted);
  auto pwallet = (CWallet*)parg;
  try {
    BitcoinMiner(pwallet, false);
    interruption_point(miner_interrupted);
  } catch (const thread_interrupted&) {  ///
    LogPrintf("%s thread interrupt\n", "BitcoinMiner");
  } catch (...) {  ///
    LogPrintf("ThreadBitcoinMiner() exception\n");
  }
  LogPrintf("ThreadBitcoinMiner exiting\n");
}

void GenerateBitcoins(bool fGenerate, CWallet* pwallet, int nThreads) {
  static std::thread miner_thread;
  fGenerateBitcoins = fGenerate;

  /*
  if (nThreads < 0) {
    // In regtest threads defaults to 1
    if (Params().DefaultMinerThreads())
      nThreads = Params().DefaultMinerThreads();
    else
      nThreads = std::thread::hardware_concurrency();
  }

  if (minerThreads != nullptr) {
    minerThreads->interrupt_all();
    delete minerThreads;
    minerThreads = nullptr;
  }
  */

  if (nThreads == 0 || !fGenerate) return;

  // Assume just 1 thread to get this going. TBD add loop later
  auto bindMiner = std::bind(ThreadBitcoinMiner, pwallet);
  miner_thread = std::thread(&TraceThread<decltype(bindMiner)>, "miner", std::move(bindMiner));
  miner_thread.detach();
}
