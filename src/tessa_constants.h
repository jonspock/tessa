// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once
#include "consensus/consensus.h"  // Mostly other defs

/** Threshold for nLockTime: below this value it is interpreted as block number, otherwise as UNIX timestamp. */
static const uint32_t LOCKTIME_THRESHOLD = 500000000;  // Tue Nov  5 00:53:20 1985 UTC

/** Flags for nSequence and nLockTime locks  -already in consensus.h */
/** Used as the flags parameter to sequence and nLocktime checks in non-consensus code. */
static const uint32_t STANDARD_LOCKTIME_VERIFY_FLAGS = LOCKTIME_VERIFY_SEQUENCE | LOCKTIME_MEDIAN_TIME_PAST;

/** The maximum allowed size for a serialized block, in bytes (network rule) */
static const uint32_t MAX_BLOCK_SIZE_CURRENT = 2000000;
static const uint32_t MAX_BLOCK_SIZE_LEGACY = 1000000;

/** Default for -blockmaxsize and -blockminsize, which control the range of sizes the mining code will create **/
static const uint32_t DEFAULT_BLOCK_MAX_SIZE = 750000;
static const uint32_t DEFAULT_BLOCK_MIN_SIZE = 0;
/** Default for -blockprioritysize, maximum space for zero/low-fee transactions **/
static const uint32_t DEFAULT_BLOCK_PRIORITY_SIZE = 50000;
/** Default for accepting alerts from the P2P network. */
static const bool DEFAULT_ALERTS = true;
/** The maximum size for transactions we're willing to relay/mine */
static const uint32_t MAX_STANDARD_TX_SIZE = 100000;
static const uint32_t MAX_ZEROCOIN_TX_SIZE = 150000;
/** The maximum allowed number of signature check operations in a block (network rule) */
static const uint32_t MAX_BLOCK_SIGOPS_CURRENT = MAX_BLOCK_SIZE_CURRENT / 50;
static const uint32_t MAX_BLOCK_SIGOPS_LEGACY = MAX_BLOCK_SIZE_LEGACY / 50;
/** Maximum number of signature check operations in an IsStandard() P2SH script */
static const uint32_t MAX_P2SH_SIGOPS = 15;
/** The maximum number of sigops we're willing to relay/mine in a single tx */
static const uint32_t MAX_TX_SIGOPS_CURRENT = MAX_BLOCK_SIGOPS_CURRENT / 5;
static const uint32_t MAX_TX_SIGOPS_LEGACY = MAX_BLOCK_SIGOPS_LEGACY / 5;
/** Default for -maxorphantx, maximum number of orphan transactions kept in memory */
static const uint32_t DEFAULT_MAX_ORPHAN_TRANSACTIONS = 100;
/** The maximum size of a blk?????.dat file (since 0.8) */
static const uint32_t MAX_BLOCKFILE_SIZE = 0x8000000;  // 128 MiB
/** The pre-allocation chunk size for blk?????.dat files (since 0.8) */
static const uint32_t BLOCKFILE_CHUNK_SIZE = 0x1000000;  // 16 MiB
/** The pre-allocation chunk size for rev?????.dat files (since 0.8) */
static const uint32_t UNDOFILE_CHUNK_SIZE = 0x100000;  // 1 MiB
/** Maximum number of script-checking threads allowed */
static const int MAX_SCRIPTCHECK_THREADS = 16;
/** -par default (number of script-checking threads, 0 = auto) */
static const int32_t DEFAULT_SCRIPTCHECK_THREADS = 0;
/** Number of blocks that can be requested at any given time from a single peer. */
static const int32_t MAX_BLOCKS_IN_TRANSIT_PER_PEER = 16;
/** Timeout in seconds during which a peer must stall block download progress before being disconnected. */
static const uint32_t BLOCK_STALLING_TIMEOUT = 2;
/** Number of headers sent in one getheaders result. We rely on the assumption that if a peer sends
 *  less than this number, we reached their tip. Changing this value is a protocol upgrade. */
static const uint32_t MAX_HEADERS_RESULTS = 2000;
/** Size of the "block download window": how far ahead of our current height do we fetch?
 *  Larger windows tolerate larger download speed differences between peer, but increase the potential
 *  degree of disordering of blocks on disk (which make reindexing and in the future perhaps pruning
 *  harder). We'll probably want to make this a per-peer adaptive value at some point. */
static const uint32_t BLOCK_DOWNLOAD_WINDOW = 1024;
/** Time to wait (in seconds) between writing blockchain state to disk. */
static const uint32_t DATABASE_WRITE_INTERVAL = 3600;
/** Maximum length of reject messages. */
static const uint32_t MAX_REJECT_MESSAGE_LENGTH = 111;

/** Enable bloom filter */
static const bool DEFAULT_PEERBLOOMFILTERS = true;

static const int32_t ACC_BLOCK_INTERVAL = 10;

/** "reject" message codes */
static const uint8_t REJECT_MALFORMED = 0x01;
static const uint8_t REJECT_INVALID = 0x10;
static const uint8_t REJECT_OBSOLETE = 0x11;
static const uint8_t REJECT_DUPLICATE = 0x12;
static const uint8_t REJECT_NONSTANDARD = 0x40;
static const uint8_t REJECT_DUST = 0x41;
static const uint8_t REJECT_INSUFFICIENTFEE = 0x42;
static const uint8_t REJECT_CHECKPOINT = 0x43;
