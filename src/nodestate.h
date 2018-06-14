// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The ClubChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "netbase.h"

class CBlockIndex;

namespace {

/** Blocks that are in flight, and that are in the queue to be downloaded. Protected by cs_main. */
struct QueuedBlock {
  uint256 hash;
  CBlockIndex* pindex;         //! Optional.
  int64_t nTime;               //! Time of "getdata" request in microseconds.
  int nValidatedQueuedBefore;  //! Number of blocks queued with validated headers (globally) at the time this one is
                               //! requested.
  bool fValidatedHeaders;      //! Whether this block has validated headers at the time of request.
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

  CNodeState() {
    fCurrentlyConnected = false;
    nMisbehavior = 0;
    fShouldBan = false;
    pindexBestKnownBlock = nullptr;
    hashLastUnknownBlock = uint256S("0");
    pindexLastCommonBlock = nullptr;
    fSyncStarted = false;
    nStallingSince = 0;
    nBlocksInFlight = 0;
    fPreferredDownload = false;
  }
};
}  // namespace
