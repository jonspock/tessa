// Copyright (c) 2012-2014 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "leveldbwrapper.h"

#include "fs.h"
#include "fs_utils.h"
#include "util.h"

#ifndef USE_LEVELDB
#include <rocksdb/cache.h>
#include <rocksdb/env.h>
#include <rocksdb/filter_policy.h>
#include <rocksdb/utilities/leveldb_options.h>
#else
#include <leveldb/cache.h>
#include <leveldb/env.h>
#include <leveldb/filter_policy.h>
#include <memenv.h>
#endif

void HandleError(const datadb::Status& status)  {
  if (status.ok()) return;
  LogPrintf("%s\n", status.ToString());
  if (status.IsCorruption()) throw datadb_error("Database corrupted");
  if (status.IsIOError()) throw datadb_error("Database I/O error");
  if (status.IsNotFound()) throw datadb_error("Database entry missing");
  throw datadb_error("Unknown database error");
}

static datadb::Options GetOptions(size_t nCacheSize) {
#ifndef USE_LEVELDB
  datadb::LevelDBOptions opt;
  // opt.block_cache = datadb::NewLRUCache(nCacheSize / 2);
  opt.write_buffer_size = nCacheSize / 4;  // up to two write buffers may be held in memory simultaneously
  opt.filter_policy = datadb::NewBloomFilterPolicy(10);
  opt.compression = datadb::kNoCompression;
  opt.max_open_files = 64;
  datadb::Options datadb_options = ConvertOptions(opt);
  datadb_options.avoid_flush_during_shutdown = true;
  datadb_options.enable_thread_tracking = true;
  datadb_options.IncreaseParallelism();
  datadb_options.OptimizeLevelStyleCompaction();
  return datadb_options;
#else
  datadb::Options options;
  options.block_cache = datadb::NewLRUCache(nCacheSize / 2);
  options.write_buffer_size = nCacheSize / 4;  // up to two write buffers may be held in memory simultaneously
  options.filter_policy = datadb::NewBloomFilterPolicy(10);
  options.compression = datadb::kNoCompression;
  options.max_open_files = 64;
  if (datadb::kMajorVersion > 1 || (datadb::kMajorVersion == 1 && datadb::kMinorVersion >= 16)) {
    // Datadb versions before 1.16 consider short writes to be corruption. Only trigger error
    // on corruption in later versions.
    options.paranoid_checks = true;
  }
  return options;
#endif
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
    penv = datadb::NewMemEnv(datadb::Env::Default());
    options.env = penv;
  } else {
    if (fWipe) {
      LogPrintf("Wiping Datadb in %s\n", path.string());
      datadb::DestroyDB(path.string(), options);
    }
    TryCreateDirectory(path);
    LogPrintf("Opening Datadb in %s\n", path.string());
  }
  datadb::Status status = datadb::DB::Open(options, path.string(), &pdb);
  HandleError(status);
  LogPrintf("Opened Datadb successfully\n");
}

CLevelDBWrapper::~CLevelDBWrapper() {
  delete pdb;
  pdb = nullptr;
#ifndef USE_LEVELDB
  // Didn't help  DestroyDB(dbpath, options);
  ///// delete options.env;
  // delete options.filter_policy;
  //  options.filter_policy = nullptr;
  // delete options.block_cache;
  //  options.block_cache = nullptr;
#else
  delete options.filter_policy;
  options.filter_policy = nullptr;
  delete options.block_cache;
  options.block_cache = nullptr;
#endif
  delete penv;
  options.env = nullptr;
}

bool CLevelDBWrapper::WriteBatch(CLevelDBBatch& batch, bool fSync)  {
  datadb::Status status = pdb->Write(fSync ? syncoptions : writeoptions, &batch.batch);
  HandleError(status);
  return true;
}
