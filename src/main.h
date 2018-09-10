// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#if defined(HAVE_CONFIG_H)
#include "tessa-config.h"
#endif

#include "main_constants.h"
#include "main_externs.h"
#include "main_functions.h"
#include "mainfile.h"
#include "scriptcheck.h"
#include "nodeid.h"

class CBloomFilter;
class CInv;
class CValidationInterface;
class CValidationState;
namespace libzerocoin {
class CoinSpend;
class PublicCoin;
}  // namespace libzerocoin

class CZerocoinMint;
struct CMintMeta;
class CBigNum;

struct CBlockTemplate;
struct CNodeStateStats;

struct CNodeSignals;
class CNode;

#include "validationstate.h"
#include "verifydb.h"

/** Register a wallet to receive updates from core */
void RegisterValidationInterface(CValidationInterface* pwalletIn);
/** Unregister a wallet from core */
void UnregisterValidationInterface(CValidationInterface* pwalletIn);
/** Unregister all wallets from core */
void UnregisterAllValidationInterfaces();
/** Push an updated transaction to all registered wallets */
void SyncWithWallets(const CTransaction& tx, const CBlock* pblock = nullptr);

/** Register with a network node to receive its signals */
void RegisterNodeSignals(CNodeSignals& nodeSignals);
/** Unregister a network node */
void UnregisterNodeSignals(CNodeSignals& nodeSignals);

/**
 * Process an incoming block. This only returns after the best known valid
 * block is made active. Note that it does not, however, guarantee that the
 * specific block passed to it has been checked for validity!
 *
 * @param[out]  state   This may be set to an Error state if any error occurred processing it, including during
 * validation/connection/etc of otherwise unrelated blocks during reorganisation; or it may be set to an Invalid
 * state if pblock is itself invalid (but this is not guaranteed even when the block is checked). If you want to
 * *possibly* get feedback on whether pblock is valid, you must also install a CValidationInterface - this will
 *  have its BlockChecked method called whenever *any* block completes validation.
 * @param[in]   pfrom   The node which we are receiving the block from; it is added to mapBlockSource and may be
 *  penalised if the block is invalid.
 * @param[in]   pblock  The block we want to process.
 * @param[out]  dbp     If pblock is stored to disk (or already there), this will be set to its location.
 * @return True if state.IsValid()
 */
bool ProcessNewBlock(CValidationState& state, CNode* pfrom, CBlock* pblock, CDiskBlockPos* dbp = nullptr);
/** Import blocks from an external file */
bool LoadExternalBlockFile(FILE* fileIn, CDiskBlockPos* dbp = nullptr);
/** Initialize a new block tree database + block data on disk */
bool InitBlockIndex();
/** Load the block tree and coins database from disk */
bool LoadBlockIndex(std::string& strError);
/** Unload database information */
void UnloadBlockIndex();
/** See whether the protocol update is enforced for connected nodes */
int ActiveProtocol();
/** Process protocol messages received from a given node */
bool ProcessMessages(CNode* pfrom);
/**
 * Send queued protocol messages to be sent to a give node.
 *
 * @param[in]   pto             The node which we are sending messages to.
 * @param[in]   fSendTrickle    When true send the trickled data, otherwise trickle the data until true.
 */
bool SendMessages(CNode* pto, bool fSendTrickle);
/** Run an instance of the script checking thread */
void ThreadScriptCheck();
/** Interrupt all script checking threads once they're out of work */
void InterruptThreadScriptCheck();

// ***TODO*** probably not the right place for these 2
/** Check whether a block hash satisfies the proof-of-work requirement specified by nBits */
bool CheckProofOfWork(uint256 hash, unsigned int nBits);

/** Check whether we are doing an initial block download (synchronizing from disk or network) */
bool IsInitialBlockDownload();
/** Format a string that describes several potential problems detected by the core */
std::string GetWarnings(std::string strFor);
/** Retrieve a transaction (from memory pool, or from disk, if possible) */
bool GetTransaction(const uint256& hash, CTransaction& tx, uint256& hashBlock, bool fAllowSlow = false);
/** Find the best known block, and make it the tip of the block chain */

bool DisconnectBlocksAndReprocess(int blocks);

// ***TODO***
double ConvertBitsToDouble(unsigned int nBits);
unsigned int GetNextWorkRequired(const CBlockIndex* pindexLast, const CBlockHeader* pblock, bool fProofOfStake);

bool ActivateBestChain(CValidationState& state, CBlock* pblock = nullptr, bool fAlreadyChecked = false);
CAmount GetBlockValue(int nHeight);

/** Get statistics from node state */
bool GetNodeStateStats(NodeId nodeid, CNodeStateStats& stats);
/** Increase a node's misbehavior score. */
void Misbehaving(NodeId nodeid, int howmuch);
/** Flush all state, indexes and buffers to disk. */
void FlushStateToDisk();

/** (try to) add transaction to memory pool **/
bool AcceptToMemoryPool(CTxMemPool& pool, CValidationState& state, const CTransaction& tx, bool fLimitFree,
                        bool* pfMissingInputs, bool fRejectInsaneFee = false, bool ignoreFees = false);

bool AcceptableInputs(CTxMemPool& pool, CValidationState& state, const CTransaction& tx, bool fLimitFree,
                      bool* pfMissingInputs, bool fRejectInsaneFee = false, bool isDSTX = false);

int GetInputAge(CTxIn& vin);

struct CNodeStateStats {
  int nMisbehavior;
  int nSyncHeight;
  int nCommonHeight;
  std::vector<int> vHeightInFlight;
};

CAmount GetMinRelayFee(const CTransaction& tx, unsigned int nBytes, bool fAllowFree);
bool MoneyRange(CAmount nValueOut);

/**
 * Check transaction inputs, and make sure any
 * pay-to-script-hash transactions are evaluating IsStandard scripts
 *
 * Why bother? To avoid denial-of-service attacks; an attacker
 * can submit a standard HASH... OP_EQUAL transaction,
 * which will get accepted into blocks. The redemption
 * script can be anything; an attacker could use a very
 * expensive-to-check-upon-redemption script like:
 *   DUP CHECKSIG DROP ... repeated 100 times... OP_1
 */

/**
 * Check for standard transaction types
 * @param[in] mapInputs    Map of previous transactions that have outputs we're spending
 * @return True if all inputs (scriptSigs) use only standard transaction forms
 */
bool AreInputsStandard(const CTransaction& tx, const CCoinsViewCache& mapInputs);

/**
 * Count ECDSA signature operations the old-fashioned (pre-0.6) way
 * @return number of sigops this transaction's outputs will produce when spent
 * @see CTransaction::FetchInputs
 */
unsigned int GetLegacySigOpCount(const CTransaction& tx);

/**
 * Count ECDSA signature operations in pay-to-script-hash inputs.
 *
 * @param[in] mapInputs Map of previous transactions that have outputs we're spending
 * @return maximum number of sigops required to validate this transaction's inputs
 * @see CTransaction::FetchInputs
 */
unsigned int GetP2SHSigOpCount(const CTransaction& tx, const CCoinsViewCache& mapInputs);

/**
 * Check whether all inputs of this transaction are valid (no double spends, scripts & sigs, amounts)
 * This does not modify the UTXO set. If pvChecks is not nullptr, script checks are pushed onto it
 * instead of being performed inline.
 */
bool CheckInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& view, bool fScriptChecks,
                 unsigned int flags, bool cacheStore, std::vector<CScriptCheck>* pvChecks = nullptr);

/** Apply the effects of this transaction on the UTXO set represented by view */
void UpdateCoins(const CTransaction& tx, CValidationState& state, CCoinsViewCache& inputs, CTxUndo& txundo,
                 int nHeight);

/** Context-independent validity checks */
bool CheckTransaction(const CTransaction& tx, bool fZerocoinActive, CValidationState& state);
bool CheckZerocoinMint(const uint256& txHash, const CTxOut& txout, CValidationState& state, bool fCheckOnly = false);
bool CheckZerocoinSpend(const CTransaction& tx, bool fVerifySignature, CValidationState& state);
bool ContextualCheckZerocoinSpend(const CTransaction& tx, const libzerocoin::CoinSpend& spend, CBlockIndex* pindex);
libzerocoin::CoinSpend TxInToZerocoinSpend(const CTxIn& txin);
bool BlockToPubcoinList(const CBlock& block, std::list<libzerocoin::PublicCoin>& listPubcoins);
bool BlockToZerocoinMintList(const CBlock& block, std::list<CZerocoinMint>& vMints);
bool BlockToMintValueVector(const CBlock& block, const libzerocoin::CoinDenomination denom,
                            std::vector<CBigNum>& vValues);
std::list<libzerocoin::CoinDenomination> ZerocoinSpendListFromBlock(const CBlock& block);
void FindMints(std::vector<CMintMeta> vMintsToFind, std::vector<CMintMeta>& vMintsToUpdate,
               std::vector<CMintMeta>& vMissingMints);
bool GetZerocoinMint(const CBigNum& bnPubcoin, uint256& txHash);
bool IsSerialKnown(const CBigNum& bnSerial);
bool IsPubcoinInBlockchain(const uint256& hashPubcoin, uint256& txid);
bool RemoveSerialFromDB(const CBigNum& bnSerial);
int GetZerocoinStartHeight();
bool IsTransactionInChain(const uint256& txId, int& nHeightTx, CTransaction& tx);
bool IsTransactionInChain(const uint256& txId, int& nHeightTx);
bool IsBlockHashInChain(const uint256& hashBlock);
void RecalculateZKPSpent();
void RecalculateZKPMinted();
bool RecalculateTessaSupply(int nHeightStart);
bool ReindexAccumulators(std::list<uint256>& listMissingCheckpoints, std::string& strError);

/**
 * Check if transaction will be final in the next block to be created.
 *
 * Calls IsFinalTx() with current block height and appropriate block time.
 *
 * See consensus/consensus.h for flag definitions.
 */
bool CheckFinalTx(const CTransaction& tx, int flags = -1);

/** Check for standard transaction types
 * @return True if all outputs (scriptPubKeys) use only standard transaction forms
 */
bool IsStandardTx(const CTransaction& tx, std::string& reason);

/** Functions for disk access for blocks */
bool WriteBlockToDisk(CBlock& block, CDiskBlockPos& pos);
bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos);
bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex);

/** Functions for validating blocks and updating the block tree */

/** Reprocess a number of blocks to try and get on the correct chain again **/
bool DisconnectBlocksAndReprocess(int blocks);

/** Context-independent validity checks */
bool CheckBlockHeader(const CBlockHeader& block, CValidationState& state, bool fCheckPOW = true);
bool CheckWork(const CBlock block, CBlockIndex* const pindexPrev);

/** Context-dependent validity checks */
bool ContextualCheckBlockHeader(const CBlockHeader& block, CValidationState& state, CBlockIndex* pindexPrev);
bool ContextualCheckBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindexPrev);

/** Check a block is completely valid from start to finish (only works on top of our current best block, with cs_main
 * held) */
bool TestBlockValidity(CValidationState& state, const CBlock& block, CBlockIndex* pindexPrev, bool fCheckPOW = true,
                       bool fCheckMerkleRoot = true);

/** Store block on disk. If dbp is provided, the file is known to already reside on disk */
bool AcceptBlock(CBlock& block, CValidationState& state, CBlockIndex** pindex, CDiskBlockPos* dbp = nullptr,
                 bool fAlreadyCheckedBlock = false);
bool AcceptBlockHeader(const CBlockHeader& block, CValidationState& state, CBlockIndex** ppindex = nullptr);

/** Find the last common block between the parameter chain and a locator. */
CBlockIndex* FindForkInGlobalIndex(const CChain& chain, const CBlockLocator& locator);

/** Mark a block as invalid. */
bool InvalidateBlock(CValidationState& state, CBlockIndex* pindex);

/** Remove invalidity status from a block and its descendants. */
bool ReconsiderBlock(CValidationState& state, CBlockIndex* pindex);

struct CBlockTemplate {
  CBlock block;
  std::vector<CAmount> vTxFees;
  std::vector<int64_t> vTxSigOps;
};
