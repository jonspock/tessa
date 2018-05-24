// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers 
// Copyright (c) 2081 The ClubChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_MAIN_H
#define BITCOIN_MAIN_H

#if defined(HAVE_CONFIG_H)
#include "config/club-config.h"
#endif

#include "mainfile.h"
#include "main_externs.h"

class CBloomFilter;
class CInv;
class CScriptCheck;
class CValidationInterface;
class CValidationState;

struct CBlockTemplate;
struct CNodeStateStats;

#include "validationstate.h"
#include "verifydb.h"

/** Default for -blockmaxsize and -blockminsize, which control the range of sizes the mining code will create **/
static const unsigned int DEFAULT_BLOCK_MAX_SIZE = 750000;
static const unsigned int DEFAULT_BLOCK_MIN_SIZE = 0;
/** Default for -blockprioritysize, maximum space for zero/low-fee transactions **/
static const unsigned int DEFAULT_BLOCK_PRIORITY_SIZE = 50000;
/** Default for accepting alerts from the P2P network. */
static const bool DEFAULT_ALERTS = true;
/** The maximum size for transactions we're willing to relay/mine */
static const unsigned int MAX_STANDARD_TX_SIZE = 100000;
static const unsigned int MAX_ZEROCOIN_TX_SIZE = 150000;
/** The maximum allowed number of signature check operations in a block (network rule) */
static const unsigned int MAX_BLOCK_SIGOPS_CURRENT = MAX_BLOCK_SIZE_CURRENT / 50;
static const unsigned int MAX_BLOCK_SIGOPS_LEGACY = MAX_BLOCK_SIZE_LEGACY / 50;
/** Maximum number of signature check operations in an IsStandard() P2SH script */
static const unsigned int MAX_P2SH_SIGOPS = 15;
/** The maximum number of sigops we're willing to relay/mine in a single tx */
static const unsigned int MAX_TX_SIGOPS_CURRENT = MAX_BLOCK_SIGOPS_CURRENT / 5;
static const unsigned int MAX_TX_SIGOPS_LEGACY = MAX_BLOCK_SIGOPS_LEGACY / 5;
/** Default for -maxorphantx, maximum number of orphan transactions kept in memory */
static const unsigned int DEFAULT_MAX_ORPHAN_TRANSACTIONS = 100;
/** The maximum size of a blk?????.dat file (since 0.8) */
static const unsigned int MAX_BLOCKFILE_SIZE = 0x8000000; // 128 MiB
/** The pre-allocation chunk size for blk?????.dat files (since 0.8) */
static const unsigned int BLOCKFILE_CHUNK_SIZE = 0x1000000; // 16 MiB
/** The pre-allocation chunk size for rev?????.dat files (since 0.8) */
static const unsigned int UNDOFILE_CHUNK_SIZE = 0x100000; // 1 MiB
/** Coinbase transaction outputs can only be spent after this number of new blocks (network rule) */
static const int COINBASE_MATURITY = 100;
/** Threshold for nLockTime: below this value it is interpreted as block number, otherwise as UNIX timestamp. */
static const unsigned int LOCKTIME_THRESHOLD = 500000000; // Tue Nov  5 00:53:20 1985 UTC
/** Maximum number of script-checking threads allowed */
static const int MAX_SCRIPTCHECK_THREADS = 16;
/** -par default (number of script-checking threads, 0 = auto) */
static const int DEFAULT_SCRIPTCHECK_THREADS = 0;
/** Number of blocks that can be requested at any given time from a single peer. */
static const int MAX_BLOCKS_IN_TRANSIT_PER_PEER = 16;
/** Timeout in seconds during which a peer must stall block download progress before being disconnected. */
static const unsigned int BLOCK_STALLING_TIMEOUT = 2;
/** Number of headers sent in one getheaders result. We rely on the assumption that if a peer sends
 *  less than this number, we reached their tip. Changing this value is a protocol upgrade. */
static const unsigned int MAX_HEADERS_RESULTS = 2000;
/** Size of the "block download window": how far ahead of our current height do we fetch?
 *  Larger windows tolerate larger download speed differences between peer, but increase the potential
 *  degree of disordering of blocks on disk (which make reindexing and in the future perhaps pruning
 *  harder). We'll probably want to make this a per-peer adaptive value at some point. */
static const unsigned int BLOCK_DOWNLOAD_WINDOW = 1024;
/** Time to wait (in seconds) between writing blockchain state to disk. */
static const unsigned int DATABASE_WRITE_INTERVAL = 3600;
/** Maximum length of reject messages. */
static const unsigned int MAX_REJECT_MESSAGE_LENGTH = 111;

/** Enable bloom filter */
 static const bool DEFAULT_PEERBLOOMFILTERS = true;

/** "reject" message codes */
static const unsigned char REJECT_MALFORMED = 0x01;
static const unsigned char REJECT_INVALID = 0x10;
static const unsigned char REJECT_OBSOLETE = 0x11;
static const unsigned char REJECT_DUPLICATE = 0x12;
static const unsigned char REJECT_NONSTANDARD = 0x40;
static const unsigned char REJECT_DUST = 0x41;
static const unsigned char REJECT_INSUFFICIENTFEE = 0x42;
static const unsigned char REJECT_CHECKPOINT = 0x43;



/** Register a wallet to receive updates from core */
void RegisterValidationInterface(CValidationInterface* pwalletIn);
/** Unregister a wallet from core */
void UnregisterValidationInterface(CValidationInterface* pwalletIn);
/** Unregister all wallets from core */
void UnregisterAllValidationInterfaces();
/** Push an updated transaction to all registered wallets */
void SyncWithWallets(const CTransaction& tx, const CBlock* pblock = NULL);

/** Register with a network node to receive its signals */
void RegisterNodeSignals(CNodeSignals& nodeSignals);
/** Unregister a network node */
void UnregisterNodeSignals(CNodeSignals& nodeSignals);

/**
 * Process an incoming block. This only returns after the best known valid
 * block is made active. Note that it does not, however, guarantee that the
 * specific block passed to it has been checked for validity!
 *
 * @param[out]  state   This may be set to an Error state if any error occurred processing it, including during validation/connection/etc of otherwise unrelated blocks during reorganisation; or it may be set to an Invalid state if pblock is itself invalid (but this is not guaranteed even when the block is checked). If you want to *possibly* get feedback on whether pblock is valid, you must also install a CValidationInterface - this will have its BlockChecked method called whenever *any* block completes validation.
 * @param[in]   pfrom   The node which we are receiving the block from; it is added to mapBlockSource and may be penalised if the block is invalid.
 * @param[in]   pblock  The block we want to process.
 * @param[out]  dbp     If pblock is stored to disk (or already there), this will be set to its location.
 * @return True if state.IsValid()
 */
bool ProcessNewBlock(CValidationState& state, CNode* pfrom, CBlock* pblock, CDiskBlockPos* dbp = NULL);
/** Import blocks from an external file */
bool LoadExternalBlockFile(FILE* fileIn, CDiskBlockPos* dbp = NULL);
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

bool ActivateBestChain(CValidationState& state, CBlock* pblock = NULL, bool fAlreadyChecked = false);
CAmount GetBlockValue(int nHeight);

/** Get statistics from node state */
bool GetNodeStateStats(NodeId nodeid, CNodeStateStats& stats);
/** Increase a node's misbehavior score. */
void Misbehaving(NodeId nodeid, int howmuch);
/** Flush all state, indexes and buffers to disk. */
void FlushStateToDisk();

/** (try to) add transaction to memory pool **/
bool AcceptToMemoryPool(CTxMemPool& pool, CValidationState& state, const CTransaction& tx, bool fLimitFree, bool* pfMissingInputs, bool fRejectInsaneFee = false, bool ignoreFees = false);

bool AcceptableInputs(CTxMemPool& pool, CValidationState& state, const CTransaction& tx, bool fLimitFree, bool* pfMissingInputs, bool fRejectInsaneFee = false, bool isDSTX = false);

int GetInputAge(CTxIn& vin);
int GetInputAgeIX(uint256 nTXHash, CTxIn& vin);
bool GetCoinAge(const CTransaction& tx, unsigned int nTxTime, uint64_t& nCoinAge);

struct CNodeStateStats {
    int nMisbehavior;
    int nSyncHeight;
    int nCommonHeight;
    std::vector<int> vHeightInFlight;
};

struct CDiskTxPos : public CDiskBlockPos {
    unsigned int nTxOffset; // after header

    ADD_SERIALIZE_METHODS;

    template <typename Stream, typename Operation>
    inline void SerializationOp(Stream& s, Operation ser_action, int nType, int nVersion)
    {
        READWRITE(*(CDiskBlockPos*)this);
        READWRITE(VARINT(nTxOffset));
    }

    CDiskTxPos(const CDiskBlockPos& blockIn, unsigned int nTxOffsetIn) : CDiskBlockPos(blockIn.nFile, blockIn.nPos), nTxOffset(nTxOffsetIn)
    {
    }

    CDiskTxPos()
    {
        SetNull();
    }

    void SetNull()
    {
        CDiskBlockPos::SetNull();
        nTxOffset = 0;
    }
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
 * This does not modify the UTXO set. If pvChecks is not NULL, script checks are pushed onto it
 * instead of being performed inline.
 */
bool CheckInputs(const CTransaction& tx, CValidationState& state, const CCoinsViewCache& view, bool fScriptChecks, unsigned int flags, bool cacheStore, std::vector<CScriptCheck>* pvChecks = NULL);

/** Apply the effects of this transaction on the UTXO set represented by view */
void UpdateCoins(const CTransaction& tx, CValidationState& state, CCoinsViewCache& inputs, CTxUndo& txundo, int nHeight);

/** Context-independent validity checks */
bool CheckTransaction(const CTransaction& tx, bool fZerocoinActive, CValidationState& state);
bool CheckZerocoinMint(const uint256& txHash, const CTxOut& txout, CValidationState& state, bool fCheckOnly = false);
bool CheckZerocoinSpend(const CTransaction& tx, bool fVerifySignature, CValidationState& state);
bool ContextualCheckZerocoinSpend(const CTransaction& tx, const libzerocoin::CoinSpend& spend, CBlockIndex* pindex);
libzerocoin::CoinSpend TxInToZerocoinSpend(const CTxIn& txin);
bool BlockToPubcoinList(const CBlock& block, list<libzerocoin::PublicCoin>& listPubcoins, bool fFilterInvalid);
bool BlockToZerocoinMintList(const CBlock& block, std::list<CZerocoinMint>& vMints);
bool BlockToMintValueVector(const CBlock& block, const libzerocoin::CoinDenomination denom, std::vector<CBigNum>& vValues);
std::list<libzerocoin::CoinDenomination> ZerocoinSpendListFromBlock(const CBlock& block, bool fFilterInvalid);
void FindMints(vector<CMintMeta> vMintsToFind, vector<CMintMeta>& vMintsToUpdate, vector<CMintMeta>& vMissingMints);
bool GetZerocoinMint(const CBigNum& bnPubcoin, uint256& txHash);
bool IsSerialKnown(const CBigNum& bnSerial);
bool IsSerialInBlockchain(const CBigNum& bnSerial, int& nHeightTx);
bool IsSerialInBlockchain(const uint256& hashSerial, int& nHeightTx, uint256& txidSpend);
bool IsSerialInBlockchain(const uint256& hashSerial, int& nHeightTx, uint256& txidSpend, CTransaction& tx);
bool IsPubcoinInBlockchain(const uint256& hashPubcoin, uint256& txid);
bool RemoveSerialFromDB(const CBigNum& bnSerial);
int GetZerocoinStartHeight();
bool IsTransactionInChain(const uint256& txId, int& nHeightTx, CTransaction& tx);
bool IsTransactionInChain(const uint256& txId, int& nHeightTx);
bool IsBlockHashInChain(const uint256& hashBlock);
void RecalculateZKPSpent();
void RecalculateZKPMinted();
bool RecalculateClubSupply(int nHeightStart);
bool ReindexAccumulators(list<uint256>& listMissingCheckpoints, string& strError);


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

bool IsFinalTx(const CTransaction& tx, int nBlockHeight = 0, int64_t nBlockTime = 0);

/**
 * Closure representing one script verification
 * Note that this stores references to the spending transaction
 */
class CScriptCheck
{
private:
    CScript scriptPubKey;
    const CTransaction* ptxTo;
    unsigned int nIn;
    unsigned int nFlags;
    bool cacheStore;
    ScriptError error;

public:
    CScriptCheck() : ptxTo(0), nIn(0), nFlags(0), cacheStore(false), error(SCRIPT_ERR_UNKNOWN_ERROR) {}
    CScriptCheck(const CCoins& txFromIn, const CTransaction& txToIn, unsigned int nInIn, unsigned int nFlagsIn, bool cacheIn) : scriptPubKey(txFromIn.vout[txToIn.vin[nInIn].prevout.n].scriptPubKey),
                                                                                                                                ptxTo(&txToIn), nIn(nInIn), nFlags(nFlagsIn), cacheStore(cacheIn), error(SCRIPT_ERR_UNKNOWN_ERROR) {}

    bool operator()();

    void swap(CScriptCheck& check)
    {
        scriptPubKey.swap(check.scriptPubKey);
        std::swap(ptxTo, check.ptxTo);
        std::swap(nIn, check.nIn);
        std::swap(nFlags, check.nFlags);
        std::swap(cacheStore, check.cacheStore);
        std::swap(error, check.error);
    }

    ScriptError GetScriptError() const { return error; }
};

/** Functions for disk access for blocks */
bool WriteBlockToDisk(CBlock& block, CDiskBlockPos& pos);
bool ReadBlockFromDisk(CBlock& block, const CDiskBlockPos& pos);
bool ReadBlockFromDisk(CBlock& block, const CBlockIndex* pindex);


/** Functions for validating blocks and updating the block tree */

/** Undo the effects of this block (with given index) on the UTXO set represented by coins.
 *  In case pfClean is provided, operation will try to be tolerant about errors, and *pfClean
 *  will be true if no problems were found. Otherwise, the return value will be false in case
 *  of problems. Note that in any case, coins may be modified. */
bool DisconnectBlock(CBlock& block, CValidationState& state, CBlockIndex* pindex, CCoinsViewCache& coins, bool* pfClean = NULL);

/** Reprocess a number of blocks to try and get on the correct chain again **/
bool DisconnectBlocksAndReprocess(int blocks);

/** Apply the effects of this block (with given index) on the UTXO set represented by coins */
bool ConnectBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindex, CCoinsViewCache& coins, bool fJustCheck, bool fAlreadyChecked = false);

/** Context-independent validity checks */
bool CheckBlockHeader(const CBlockHeader& block, CValidationState& state, bool fCheckPOW = true);
bool CheckBlock(const CBlock& block, CValidationState& state, bool fCheckPOW = true, bool fCheckMerkleRoot = true, bool fCheckSig = true);
bool CheckWork(const CBlock block, CBlockIndex* const pindexPrev);

/** Context-dependent validity checks */
bool ContextualCheckBlockHeader(const CBlockHeader& block, CValidationState& state, CBlockIndex* pindexPrev);
bool ContextualCheckBlock(const CBlock& block, CValidationState& state, CBlockIndex* pindexPrev);

/** Check a block is completely valid from start to finish (only works on top of our current best block, with cs_main held) */
bool TestBlockValidity(CValidationState& state, const CBlock& block, CBlockIndex* pindexPrev, bool fCheckPOW = true, bool fCheckMerkleRoot = true);

/** Store block on disk. If dbp is provided, the file is known to already reside on disk */
bool AcceptBlock(CBlock& block, CValidationState& state, CBlockIndex** pindex, CDiskBlockPos* dbp = NULL, bool fAlreadyCheckedBlock = false);
bool AcceptBlockHeader(const CBlockHeader& block, CValidationState& state, CBlockIndex** ppindex = NULL);


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

namespace
{
    
/** Blocks that are in flight, and that are in the queue to be downloaded. Protected by cs_main. */
struct QueuedBlock {
    uint256 hash;
    CBlockIndex* pindex;        //! Optional.
    int64_t nTime;              //! Time of "getdata" request in microseconds.
    int nValidatedQueuedBefore; //! Number of blocks queued with validated headers (globally) at the time this one is requested.
    bool fValidatedHeaders;     //! Whether this block has validated headers at the time of request.
};

struct CBlockReject {
    unsigned char chRejectCode;
    string strRejectReason;
    uint256 hashBlock;
};
/**
 * Maintain validation-specific state about nodes, protected by cs_main, instead
 * by CNode's own locks. This simplifies asynchronous operation, where
 * processing of incoming data is done after the ProcessMessage call returns,
 * and we're no longer holding the node's locks.
 */
struct CNodeState {
    //! The peer's address
    CService address;
    //! Whether we have a fully established connection.
    bool fCurrentlyConnected;
    //! Accumulated misbehaviour score for this peer.
    int nMisbehavior;
    //! Whether this peer should be disconnected and banned (unless whitelisted).
    bool fShouldBan;
    //! String name of this peer (debugging/logging purposes).
    std::string name;
    //! List of asynchronously-determined block rejections to notify this peer about.
    std::vector<CBlockReject> rejects;
    //! The best known block we know this peer has announced.
    CBlockIndex* pindexBestKnownBlock;
    //! The hash of the last unknown block this peer has announced.
    uint256 hashLastUnknownBlock;
    //! The last full block we both have.
    CBlockIndex* pindexLastCommonBlock;
    //! Whether we've started headers synchronization with this peer.
    bool fSyncStarted;
    //! Since when we're stalling block download progress (in microseconds), or 0.
    int64_t nStallingSince;
    list<QueuedBlock> vBlocksInFlight;
    int nBlocksInFlight;
    //! Whether we consider this a preferred download peer.
    bool fPreferredDownload;

    CNodeState()
    {
        fCurrentlyConnected = false;
        nMisbehavior = 0;
        fShouldBan = false;
        pindexBestKnownBlock = NULL;
        hashLastUnknownBlock = uint256(0);
        pindexLastCommonBlock = NULL;
        fSyncStarted = false;
        nStallingSince = 0;
        nBlocksInFlight = 0;
        fPreferredDownload = false;
    }
};

CNodeState* State(NodeId pnode);
}

#endif // BITCOIN_MAIN_H
