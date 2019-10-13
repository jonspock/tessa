// Copyright (c) 2012-2013 The PPCoin developers
// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2016-2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "txdb.h"
#include "chainparams.h"
#include "fs.h"
#include "fs_utils.h"
#include "main.h"
#include "mainfile.h"
#include "pow.h"
#include "staker.h"
#include "uint256.h"
#include <cstdint>

using namespace std;

CBlockTreeDB::CBlockTreeDB(size_t nCacheSize, bool fMemory, bool fWipe)
    : CDataDBWrapper(GetDataDir() / "blocks" / "index", nCacheSize, fMemory, fWipe) {}

bool CBlockTreeDB::WriteBlockIndex(const CDiskBlockIndex& blockindex) {
  return Write(make_pair('b', blockindex.GetBlockHash()), blockindex);
}

bool CBlockTreeDB::WriteBlockFileInfo(int nFile, const CBlockFileInfo& info) {
  return Write(make_pair('f', nFile), info);
}

bool CBlockTreeDB::ReadBlockFileInfo(int nFile, CBlockFileInfo& info) { return Read(make_pair('f', nFile), info); }

bool CBlockTreeDB::WriteLastBlockFile(int nFile) { return Write('l', nFile); }

bool CBlockTreeDB::WriteReindexing(bool fReindexing) {
  if (fReindexing)
    return Write('R', '1');
  else
    return Erase('R');
}

bool CBlockTreeDB::ReadReindexing() { return Exists('R'); }

int CBlockTreeDB::ReadLastBlockFile() {
  int nFile;
  Read('l', nFile);
  return nFile;
}

bool CBlockTreeDB::ReadTxIndex(const uint256& txid, CDiskTxPos& pos) { return Read(make_pair('t', txid), pos); }

bool CBlockTreeDB::WriteTxIndex(const std::vector<std::pair<uint256, CDiskTxPos> >& vect) {
  CDataDBBatch batch;
  for (auto& it : vect) batch.Write(make_pair('t', it.first), it.second);
  return WriteBatch(batch);
}

bool CBlockTreeDB::WriteFlag(const std::string& name, bool fValue) {
  return Write(std::make_pair('F', name), fValue ? '1' : '0');
}

bool CBlockTreeDB::ReadFlag(const std::string& name) {
  char ch;
  if (!Read(std::make_pair('F', name), ch)) return false;
  return ch == '1';
}

// bool CBlockTreeDB::WriteInt(const std::string& name, int nValue) { return Write(std::make_pair('I', name), nValue); }
// bool CBlockTreeDB::ReadInt(const std::string& name, int& nValue) { return Read(std::make_pair('I', name), nValue); }

bool CBlockTreeDB::LoadBlockIndexGuts() {
  std::unique_ptr<datadb::Iterator> pcursor(NewIterator());

  CDataStream ssKeySet(SER_DISK, CLIENT_VERSION);
  ssKeySet << make_pair('b', uint256());
  pcursor->Seek(ssKeySet.str());

  // Load mapBlockIndex
  uint256 nPreviousCheckpoint = uint256();
  nPreviousCheckpoint.SetNull();
  while (pcursor->Valid()) {
    if (interrupt) return error("LoadBlockIndexGuts() : interrupted");
    try {
      datadb::Slice slKey = pcursor->key();
      CDataStream ssKey(slKey.data(), slKey.data() + slKey.size(), SER_DISK, CLIENT_VERSION);
      char chType;
      ssKey >> chType;
      if (chType == 'b') {
        datadb::Slice slValue = pcursor->value();
        CDataStream ssValue(slValue.data(), slValue.data() + slValue.size(), SER_DISK, CLIENT_VERSION);
        CDiskBlockIndex diskindex;
        ssValue >> diskindex;

        // Construct block index object
        CBlockIndex* pindexNew = InsertBlockIndex(diskindex.GetBlockHash());
        pindexNew->pprev = InsertBlockIndex(diskindex.hashPrev);
        pindexNew->pnext = InsertBlockIndex(diskindex.hashNext);
        pindexNew->nHeight = diskindex.nHeight;
        pindexNew->nFile = diskindex.nFile;
        pindexNew->nDataPos = diskindex.nDataPos;
        pindexNew->nUndoPos = diskindex.nUndoPos;
        pindexNew->nHeaderVersion = diskindex.nHeaderVersion;
        pindexNew->hashMerkleRoot = diskindex.hashMerkleRoot;
        pindexNew->nTime = diskindex.nTime;
        pindexNew->nBits = diskindex.nBits;
        pindexNew->nNonce = diskindex.nNonce;
        pindexNew->nStatus = diskindex.nStatus;
        pindexNew->nTx = diskindex.nTx;

        // Proof Of Stake
        pindexNew->nMint = diskindex.nMint;
        pindexNew->nMoneySupply = diskindex.nMoneySupply;
        pindexNew->nFlags = diskindex.nFlags;
        pindexNew->nStakeModifier = diskindex.nStakeModifier;
        pindexNew->prevoutStake = diskindex.prevoutStake;
        pindexNew->nStakeTime = diskindex.nStakeTime;
        pindexNew->hashProofOfStake = diskindex.hashProofOfStake;

        if (pindexNew->nHeight <= Params().LAST_POW_BLOCK()) {
          if (!CheckProofOfWork(pindexNew->GetBlockHash(), pindexNew->nBits))
            return error("LoadBlockIndex() : CheckProofOfWork failed: %s", pindexNew->ToString());
        }
        // ppcoin: build setStakeSeen
        if (pindexNew->IsProofOfStake()) gStaker.setSeen(make_pair(pindexNew->prevoutStake, pindexNew->nStakeTime));

        pcursor->Next();
      } else {
        break;  // if shutdown requested or finished loading block index
      }
    } catch (std::exception& e) { return error("%s : Deserialize or I/O error - %s", __func__, e.what()); }
  }

  return true;
}

void CBlockTreeDB::InterruptLoadBlockIndexGuts() { interrupt = true; }
