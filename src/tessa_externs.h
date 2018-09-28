// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <cstdint>
#include <map>
#include <set>
#include <string>

#include "sync.h"
// from sync.h  CCriticalSection, CConditionVariable, CWaitableCriticalSection;

class CBlockIndex;
class CBlockTreeDB;
class CChain;
class CZerocoinDB;
class CTxMemPool;
class CFeeRate;
class CCoinsViewCache;
class uint256;
class CScript;

extern CScript COINBASE_FLAGS;
extern CCriticalSection cs_main;
extern CTxMemPool mempool;
extern uint64_t nLastBlockTx;
extern uint64_t nLastBlockSize;
extern const std::string strMessageMagic;
extern int64_t nTimeBestReceived;
extern CWaitableCriticalSection csBestBlock;
extern CConditionVariable cvBlockChange;
extern int32_t nScriptCheckThreads;
extern uint32_t nCoinCacheSize;
extern CFeeRate minRelayTxFee;
extern int64_t nReserveBalance;

extern bool fImporting;
extern bool fReindex;
extern bool fTxIndex;
extern bool fIsBareMultisigStd;
extern bool fCheckBlockIndex;
extern bool fVerifyingBlocks;
extern bool fDisableWallet;


extern std::map<uint32_t, uint32_t> mapHashedBlocks;
extern std::map<uint256, int64_t> mapZerocoinspends;  // txid, time received

/** Best header we've seen so far (used for getheaders queries' starting points). */
extern CBlockIndex* pindexBestHeader;
// used in main and warnings
extern CBlockIndex* pindexBestInvalid;

/** The currently-connected chain of blocks. */
extern CChain chainActive;

/** Global variable that points to the active CCoinsView (protected by cs_main) */
extern CCoinsViewCache* gpCoinsTip;

/** Global variable that points to the active block tree (protected by cs_main) */
extern CBlockTreeDB* gpBlockTreeDB;

/** Global variable that points to the zerocoin database (protected by cs_main) */
extern CZerocoinDB* gpZerocoinDB;

