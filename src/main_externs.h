// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <map>
#include <set>
#include <cstdint>
#include <string>

class CBlockIndex;
class CBlockTreeDB;
class CChain;
class CZerocoinDB;
class CSporkDB;
class CTxMemPool;
class CFeeRate;
class CCoinsViewCache;
class uint256;

extern CScript COINBASE_FLAGS;
extern CCriticalSection cs_main;
extern CTxMemPool mempool;
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

extern std::map<unsigned int, unsigned int> mapHashedBlocks;
extern std::map<uint256, int64_t> mapZerocoinspends;  // txid, time received

/** Best header we've seen so far (used for getheaders queries' starting points). */
extern CBlockIndex* pindexBestHeader;
// used in main and warnings
extern CBlockIndex* pindexBestInvalid;

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

