// Copyright (c) 2012-2014 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "leveldbwrapper.h"

#include "fs.h"
#include "util.h"

#include <rocksdb/cache.h>
#include <rocksdb/env.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/utilities/leveldb_options.h>

void HandleError(const rocksdb::Status& status) throw(leveldb_error) {
  if (status.ok()) return;
  LogPrintf("%s\n", status.ToString());
  if (status.IsCorruption()) throw leveldb_error("Database corrupted");
  if (status.IsIOError()) throw leveldb_error("Database I/O error");
  if (status.IsNotFound()) throw leveldb_error("Database entry missing");
  throw leveldb_error("Unknown database error");
}

static rocksdb::Options GetOptions(size_t nCacheSize) {
  rocksdb::LevelDBOptions opt;
  //opt.block_cache = rocksdb::NewLRUCache(nCacheSize / 2);
  opt.write_buffer_size = nCacheSize / 4;  // up to two write buffers may be held in memory simultaneously
  opt.filter_policy = rocksdb::NewBloomFilterPolicy(10);
  opt.compression = rocksdb::kNoCompression;
  opt.max_open_files = 64;
  rocksdb::Options rocksdb_options = ConvertOptions(opt);  
  return rocksdb_options;
}

CLevelDBWrapper::CLevelDBWrapper(const fs::path& path, size_t nCacheSize, bool fMemory, bool fWipe) {
  penv = nullptr;
  readoptions.verify_checksums = true;
  iteroptions.verify_checksums = true;
  iteroptions.fill_cache = false;
  syncoptions.sync = true;
  options = GetOptions(nCacheSize);
  options.create_if_missing = true;
  if (fMemory) {
    penv = rocksdb::NewMemEnv(rocksdb::Env::Default());
    options.env = penv;
  } else {
    if (fWipe) {
      LogPrintf("Wiping Rocksdb in %s\n", path.string());
      rocksdb::DestroyDB(path.string(), options);
    }
    TryCreateDirectory(path);
    LogPrintf("Opening Rocksdb in %s\n", path.string());
  }
  rocksdb::Status status = rocksdb::DB::Open(options, path.string(), &pdb);
  HandleError(status);
  LogPrintf("Opened Rocksdb successfully\n");
}

CLevelDBWrapper::~CLevelDBWrapper() {
  delete pdb;
  pdb = nullptr;
  //delete options.filter_policy;
  //  options.filter_policy = nullptr;
  //delete options.block_cache;
  //  options.block_cache = nullptr;
  delete penv;
  options.env = nullptr;
}

bool CLevelDBWrapper::WriteBatch(CLevelDBBatch& batch, bool fSync) throw(leveldb_error) {
  rocksdb::Status status = pdb->Write(fSync ? syncoptions : writeoptions, &batch.batch);
  HandleError(status);
  return true;
}
