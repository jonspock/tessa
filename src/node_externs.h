// Copyright (c) 2018 The ClubChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "nodestate.h"

extern map<uint256, pair<NodeId, list<QueuedBlock>::iterator> > gMapBlocksInFlight;
extern map<NodeId, CNodeState> gMapNodeState;

namespace {
extern CNodeState* State(NodeId pnode);
}    
void EraseOrphansFor(NodeId peer);
