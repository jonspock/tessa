// Copyright (c) 2012-2014 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "clientversion.h"
#include "fs.h"
#include "serialize.h"
#include "streams.h"
#include "util.h"
#include "version.h"

#include <rocksdb/db.h>
#include <rocksdb/write_batch.h>

class leveldb_error : public std::runtime_error {
 public:
  leveldb_error(const std::string& msg) : std::runtime_error(msg) {}
};

void HandleError(const rocksdb::Status& status) 

/** Batch of changes queued to be written to a CLevelDBWrapper */
class CLevelDBBatch {
  friend class CLevelDBWrapper;

 private:
  rocksdb::WriteBatch batch;

 public:
  template <typename K, typename V> void Write(const K& key, const V& value) {
    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(ssKey.GetSerializeSize(key));
    ssKey << key;
    rocksdb::Slice slKey(&ssKey[0], ssKey.size());

    CDataStream ssValue(SER_DISK, CLIENT_VERSION);
    ssValue.reserve(ssValue.GetSerializeSize(value));
    ssValue << value;
    rocksdb::Slice slValue(&ssValue[0], ssValue.size());

    batch.Put(slKey, slValue);
  }

  template <typename K> void Erase(const K& key) {
    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(ssKey.GetSerializeSize(key));
    ssKey << key;
    rocksdb::Slice slKey(&ssKey[0], ssKey.size());

    batch.Delete(slKey);
  }
};

class CLevelDBWrapper {
 private:
  //! custom environment this database is using (may be nullptr in case of default environment)
  rocksdb::Env* penv;

  //! database options used
  rocksdb::Options options;

  //! options used when reading from the database
  rocksdb::ReadOptions readoptions;

  //! options used when iterating over values of the database
  rocksdb::ReadOptions iteroptions;

  //! options used when writing to the database
  rocksdb::WriteOptions writeoptions;

  //! options used when sync writing to the database
  rocksdb::WriteOptions syncoptions;

  //! the database itself
  rocksdb::DB* pdb;

 public:
  CLevelDBWrapper(const fs::path& path, size_t nCacheSize, bool fMemory = false, bool fWipe = false);
  ~CLevelDBWrapper();

  template <typename K, typename V> bool Read(const K& key, V& value) const {
    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(ssKey.GetSerializeSize(key));
    ssKey << key;
    rocksdb::Slice slKey(&ssKey[0], ssKey.size());

    std::string strValue;
    rocksdb::Status status = pdb->Get(readoptions, slKey, &strValue);
    if (!status.ok()) {
      if (status.IsNotFound()) return false;
      LogPrintf("Rocksdb read failure: %s\n", status.ToString());
      HandleError(status);
    }
    try {
      CDataStream ssValue(strValue.data(), strValue.data() + strValue.size(), SER_DISK, CLIENT_VERSION);
      ssValue >> value;
    } catch (const std::exception&) { return false; }
    return true;
  }

  template <typename K, typename V> bool Write(const K& key, const V& value, bool fSync = false) {
    CLevelDBBatch batch;
    batch.Write(key, value);
    return WriteBatch(batch, fSync);
  }

  template <typename K> bool Exists(const K& key) const {
    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(ssKey.GetSerializeSize(key));
    ssKey << key;
    rocksdb::Slice slKey(&ssKey[0], ssKey.size());

    std::string strValue;
    rocksdb::Status status = pdb->Get(readoptions, slKey, &strValue);
    if (!status.ok()) {
      if (status.IsNotFound()) return false;
      LogPrintf("Rocksdb read failure: %s\n", status.ToString());
      HandleError(status);
    }
    return true;
  }

  template <typename K> bool Erase(const K& key, bool fSync = false) {
    CLevelDBBatch batch;
    batch.Erase(key);
    return WriteBatch(batch, fSync);
  }

  bool WriteBatch(CLevelDBBatch& batch, bool fSync = false);

  // not available for Rocksdb; provide for compatibility with BDB
  bool Flush() { return true; }

  bool Sync() throw(leveldb_error) {
    CLevelDBBatch batch;
    return WriteBatch(batch, true);
  }

  // not exactly clean encapsulation, but it's easiest for now
  rocksdb::Iterator* NewIterator() { return pdb->NewIterator(iteroptions); }
};
