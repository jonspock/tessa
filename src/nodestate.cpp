#include "nodestate.h"
#include "main_externs.h"
#include "node_externs.h"

/** Map maintaining per-node state. Requires cs_main. */
extern map<NodeId, CNodeState> gMapNodeState;
extern map<uint256, pair<NodeId, list<QueuedBlock>::iterator> > gMapBlocksInFlight;
/** Number of nodes with fSyncStarted. */
int nSyncStarted = 0;
/** Number of preferable block download peers. */
int nPreferredDownload = 0;


bool GetNodeStateStats(NodeId nodeid, CNodeStateStats& stats) {
  LOCK(cs_main);
  CNodeState* state = State(nodeid);
  if (state == nullptr) return false;
  stats.nMisbehavior = state->nMisbehavior;
  stats.nSyncHeight = state->pindexBestKnownBlock ? state->pindexBestKnownBlock->nHeight : -1;
  stats.nCommonHeight = state->pindexLastCommonBlock ? state->pindexLastCommonBlock->nHeight : -1;
  for (const QueuedBlock& queue : state->vBlocksInFlight) {
    if (queue.pindex) stats.vHeightInFlight.push_back(queue.pindex->nHeight);
  }
  return true;
}

// Requires cs_main.
void Misbehaving(NodeId pnode, int howmuch) {
  if (howmuch == 0) return;

  CNodeState* state = State(pnode);
  if (state == nullptr) return;

  state->nMisbehavior += howmuch;
  int banscore = GetArg("-banscore", 100);
  if (state->nMisbehavior >= banscore && state->nMisbehavior - howmuch < banscore) {
    LogPrintf("Misbehaving: %s (%d -> %d) BAN THRESHOLD EXCEEDED\n", state->name, state->nMisbehavior - howmuch,
              state->nMisbehavior);
    state->fShouldBan = true;
  } else
    LogPrintf("Misbehaving: %s (%d -> %d)\n", state->name, state->nMisbehavior - howmuch, state->nMisbehavior);
}

void InitializeNode(NodeId nodeid, const CNode* pnode) {
  LOCK(cs_main);
  CNodeState& state = gMapNodeState.insert(std::make_pair(nodeid, CNodeState())).first->second;
  state.name = pnode->addrName;
  state.address = pnode->addr;
}

void FinalizeNode(NodeId nodeid) {
  LOCK(cs_main);
  CNodeState* state = State(nodeid);

  if (state->fSyncStarted) nSyncStarted--;

  if (state->nMisbehavior == 0 && state->fCurrentlyConnected) { AddressCurrentlyConnected(state->address); }

  for (const QueuedBlock& entry : state->vBlocksInFlight) gMapBlocksInFlight.erase(entry.hash);
  EraseOrphansFor(nodeid);
  nPreferredDownload -= state->fPreferredDownload;

  gMapNodeState.erase(nodeid);
}
