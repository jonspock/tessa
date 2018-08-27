// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

// May not need all of these

#include "amount.h"
#include "chain.h"
#include "chainparams.h"
#include "coins.h"
#include "net.h"
#include "primitives/block.h"
#include "primitives/transaction.h"
#include "primitives/zerocoin.h"
#include "script/script.h"
#include "script/sigcache.h"
#include "script/standard.h"
#include "sync.h"
#include "txmempool.h"
#include "uint256.h"
#include "undo.h"

#include <map>
#include <set>
#include <stdint.h>
#include <string>
#include <utility>

#include <unordered_map>

struct BlockHasher {
  size_t operator()(const uint256& hash) const { return hash.GetLow64(); }
};

class CBlockIndex;
class CBlockTreeDB;
class CZerocoinDB;
class CSporkDB;

extern CScript COINBASE_FLAGS;
extern CCriticalSection cs_main;
extern CTxMemPool mempool;
typedef std::unordered_map<uint256, CBlockIndex*, BlockHasher> BlockMap;
extern BlockMap mapBlockIndex;
extern uint64_t nLastBlockTx;
extern uint64_t nLastBlockSize;
extern const std::string strMessageMagic;
extern int64_t nTimeBestReceived;
extern CWaitableCriticalSection csBestBlock;
extern CConditionVariable cvBlockChange;
extern bool fImporting;
extern bool fReindex;
extern int nScriptCheckThreads;
extern bool fTxIndex;
extern bool fIsBareMultisigStd;
extern bool fCheckBlockIndex;
extern unsigned int nCoinCacheSize;
extern CFeeRate minRelayTxFee;
extern bool fVerifyingBlocks;
extern bool fDisableWallet;

extern bool fLargeWorkForkFound;
extern bool fLargeWorkInvalidChainFound;

extern int64_t nReserveBalance;

extern std::map<uint256, int64_t> mapRejectedBlocks;
extern std::map<unsigned int, unsigned int> mapHashedBlocks;
extern std::map<uint256, int64_t> mapZerocoinspends;  // txid, time received

/** Best header we've seen so far (used for getheaders queries' starting points). */
extern CBlockIndex* pindexBestHeader;

/** The currently-connected chain of blocks. */
extern CChain chainActive;

/** Global variable that points to the active CCoinsView (protected by cs_main) */
extern CCoinsViewCache* pcoinsTip;

/** Global variable that points to the active block tree (protected by cs_main) */
extern CBlockTreeDB* pblocktree;

/** Global variable that points to the zerocoin database (protected by cs_main) */
extern CZerocoinDB* zerocoinDB;

/** Global variable that points to the spork database (protected by cs_main) */
extern CSporkDB* pSporkDB;

// used in main and warnings
extern CBlockIndex* pindexBestInvalid;
