// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <cstdint>
#include "sync.h"
// from sync.h  CCriticalSection, CConditionVariable, CWaitableCriticalSection;

class CBlockIndex;
class CBlockTreeDB;
class CChain;
class CZerocoinDB;
class CTxMemPool;
class CCoinsViewCache;
class CScript;

extern CScript COINBASE_FLAGS;
extern CWaitableCriticalSection csBestBlock;
extern CConditionVariable cvBlockChange;
extern CCriticalSection cs_main;
extern CTxMemPool mempool;

extern int32_t nScriptCheckThreads;
extern int64_t nTimeBestReceived;

extern bool fImporting;
extern bool fReindex;
extern bool fTxIndex;
extern bool fIsBareMultisigStd;
extern bool fCheckBlockIndex;
extern bool fVerifyingBlocks;

/** Best header we've seen so far (used for getheaders queries' starting points). */
extern CBlockIndex* pindexBestHeader;
// used in main and warnings
extern CBlockIndex* pindexBestInvalid;

/** The currently-connected chain of blocks. */
extern CChain chainActive;

/** Global variable that points to the active CCoinsView (protected by cs_main) */
extern CCoinsViewCache* gpCoinsTip;

/** Global variable that points to the active block tree (protected by cs_main) */
extern CBlockTreeDB* gpBlockTreeDB; // init.cpp/main.cpp 

/** Global variable that points to the zerocoin database (protected by cs_main) */
extern CZerocoinDB* gpZerocoinDB;

