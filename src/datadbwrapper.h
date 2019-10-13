// Copyright (c) 2012-2014 The Bitcoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "clientversion.h"
#include "fs_utils.h"
#include "serialize.h"
#include "streams.h"
#include "util.h"
#include "version.h"

#ifndef USE_LEVELDB
#include <rocksdb/db.h>
#include <rocksdb/write_batch.h>
namespace datadb = rocksdb;
#else
#include <leveldb/db.h>
#include <leveldb/write_batch.h>
namespace datadb = leveldb;
#endif

class datadb_error : public std::runtime_error {
 public:
  datadb_error(const std::string& msg) : std::runtime_error(msg) {}
};

void HandleError(const datadb::Status& status);

/** Batch of changes queued to be written to a CDataDBWrapper */
class CDataDBBatch {
  friend class CDataDBWrapper;

 private:
  datadb::WriteBatch batch;

 public:
  template <typename K, typename V> void Write(const K& key, const V& value) {
    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(ssKey.GetSerializeSize(key));
    ssKey << key;
    datadb::Slice slKey(&ssKey[0], ssKey.size());

    CDataStream ssValue(SER_DISK, CLIENT_VERSION);
    ssValue.reserve(ssValue.GetSerializeSize(value));
    ssValue << value;
    datadb::Slice slValue(&ssValue[0], ssValue.size());

    batch.Put(slKey, slValue);
  }

  template <typename K> void Erase(const K& key) {
    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(ssKey.GetSerializeSize(key));
    ssKey << key;
    datadb::Slice slKey(&ssKey[0], ssKey.size());

    batch.Delete(slKey);
  }
};

class CDataDBWrapper {
 private:
  //! custom environment this database is using (may be nullptr in case of default environment)
  datadb::Env* penv;

  //! database options used
  datadb::Options options;

  //! options used when reading from the database
  datadb::ReadOptions readoptions;

  //! options used when iterating over values of the database
  datadb::ReadOptions iteroptions;

  //! options used when writing to the database
  datadb::WriteOptions writeoptions;

  //! options used when sync writing to the database
  datadb::WriteOptions syncoptions;

  //! the database itself
  datadb::DB* pdb;

 public:
  CDataDBWrapper(const fs::path& path, size_t nCacheSize, bool fMemory = false, bool fWipe = false);
  ~CDataDBWrapper();

  template <typename K, typename V> bool Read(const K& key, V& value) const {
    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(ssKey.GetSerializeSize(key));
    ssKey << key;
    datadb::Slice slKey(&ssKey[0], ssKey.size());

    std::string strValue;
    datadb::Status status = pdb->Get(readoptions, slKey, &strValue);
    if (!status.ok()) {
      if (status.IsNotFound()) return false;
      LogPrintf("DataDB read failure: %s\n", status.ToString());
      HandleError(status);
    }
    try {
      CDataStream ssValue(strValue.data(), strValue.data() + strValue.size(), SER_DISK, CLIENT_VERSION);
      ssValue >> value;
    } catch (const std::exception&) { return false; }
    return true;
  }

  template <typename K, typename V> bool Write(const K& key, const V& value, bool fSync = false) {
    CDataDBBatch batch;
    batch.Write(key, value);
    return WriteBatch(batch, fSync);
  }

  template <typename K> bool Exists(const K& key) const {
    CDataStream ssKey(SER_DISK, CLIENT_VERSION);
    ssKey.reserve(ssKey.GetSerializeSize(key));
    ssKey << key;
    datadb::Slice slKey(&ssKey[0], ssKey.size());

    std::string strValue;
    datadb::Status status = pdb->Get(readoptions, slKey, &strValue);
    if (!status.ok()) {
      if (status.IsNotFound()) return false;
      LogPrintf("DataDB read failure: %s\n", status.ToString());
      HandleError(status);
    }
    return true;
  }

  template <typename K> bool Erase(const K& key, bool fSync = false) {
    CDataDBBatch batch;
    batch.Erase(key);
    return WriteBatch(batch, fSync);
  }

  bool WriteBatch(CDataDBBatch& batch, bool fSync = false);

  // not available for Datadb; provide for compatibility with BDB
  bool Flush() { return true; }

  bool Sync() {
    CDataDBBatch batch;
    return WriteBatch(batch, true);
  }

  // not exactly clean encapsulation, but it's easiest for now
  datadb::Iterator* NewIterator() { return pdb->NewIterator(iteroptions); }
};
