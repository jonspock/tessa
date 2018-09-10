#pragma once

#include "chain.h"
#include "uint256.h"

bool AbortNode(const std::string& strMessage, const std::string& userMessage = "");

/** Check whether enough disk space is available for an incoming block */
bool CheckDiskSpace(uint64_t nAdditionalBytes = 0);

FILE* OpenDiskFile(const CDiskBlockPos& pos, const char* prefix, bool fReadOnly = false);

/** Open a block file (blk?????.dat) */
FILE* OpenBlockFile(const CDiskBlockPos& pos, bool fReadOnly = false);

/** Open an undo file (rev?????.dat) */
FILE* OpenUndoFile(const CDiskBlockPos& pos, bool fReadOnly = false);

/** Create a new block index entry for a given block hash */
CBlockIndex* InsertBlockIndex(uint256 hash);
