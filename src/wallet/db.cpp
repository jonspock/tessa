// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2018 The Bitcoin developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "db.h"
#include "util.h"
#include <cstdint>

#ifndef WIN32
#include <sys/stat.h>
#endif

// Assuming same path as Environment
bool CDB::init(const fs::path& wallet_dir, const char* pszMode) {
  if (env) return false;  // Already setup
  // check?
  if (mdb_env_create(&env)) throw std::runtime_error("Failed on creating Db Env");
  fDbEnvInit = false;
  interrupt = false;
  return open(wallet_dir, pszMode);
}
void CDB::Interrupt()
{
    interrupt = true;
}

bool CDB::open(const fs::path& wallet_dir, const char* pszMode) {
  if (fDbEnvInit) return false;

  interruption_point(interrupt);

  TryCreateDirectory(wallet_dir);

  LogPrintf("CDBEnv::Open: %s\n", wallet_dir.string());

  int dbr = mdb_env_set_mapsize(env, 10485760);
  dbr |= mdb_env_set_maxdbs(env, 4);
  dbr |= mdb_env_open(env, wallet_dir.c_str(), 0, 0664);  // MDB_FIXEDMAP | MDB_NOSYNC, 0664);

  if (dbr != 0) {
    LogPrintf("CDBEnv::Open: Error opening database env %s\n", wallet_dir.string());
    Close();
    return dbr;
  }

  fReadOnly = (!strchr(pszMode, '+') && !strchr(pszMode, 'w'));
  bool fCreate = false;

  activeTxn = TxnBegin();

  {
    LOCK(cs_db);
    // Check if created or not

    dbr = mdb_dbi_open(activeTxn, wallet_dir.c_str(), MDB_DUPSORT, &dbi);
    if (dbr == 0) {
      LogPrintf("Open old DBI OK\n");
    } else if (!fReadOnly) {
      dbr = mdb_dbi_open(activeTxn,  // Txn pointer
                         wallet_dir.c_str(), MDB_CREATE | MDB_DUPSORT, &dbi);
      fCreate = true;
    }
    if (dbr != 0) {
      LogPrintf("CDBEnv::Open: Error opening database dbi %s\n", wallet_dir.string());
      Close();
      return dbr;
    }

    if (fCreate) WriteVersion(CLIENT_VERSION);
  }
  fDbEnvInit = true;
  return false;
}

MDB_cursor* CDB::GetCursor() {
  MDB_cursor* pcursor = nullptr;
  if (!activeTxn) activeTxn = TxnBegin();
  int dbr = mdb_cursor_open(activeTxn, dbi, &pcursor);
  if (dbr != 0) return nullptr;
  return pcursor;
}

MDB_txn* CDB::TxnBegin() {
  if (activeTxn) return activeTxn;
  MDB_txn* ptxn = nullptr;
  int dbr = mdb_txn_begin(env, nullptr, 0, &ptxn);
  if (!ptxn || dbr != 0) return nullptr;
  return ptxn;
}
// Only for Read Only DBs
MDB_txn* CDB::ReadBegin() {
  if (activeTxn) return activeTxn;
  MDB_txn* ptxn = nullptr;
  int dbr = mdb_txn_begin(env, nullptr, MDB_RDONLY, &ptxn);
  if (!ptxn || dbr != 0) return nullptr;
  return ptxn;
}

// Cursor only currently used in read only situations, not used
bool CDB::cursor_commit(MDB_cursor* pcursor) {
  mdb_cursor_close(pcursor);
  int dbr = mdb_txn_commit(activeTxn);
  activeTxn = nullptr;
  return dbr;
}
void CDB::cursor_close(MDB_cursor* pcursor) { mdb_cursor_close(pcursor); }

void CDB::Close() {
  fDbEnvInit = false;
  if (activeTxn) mdb_txn_abort(activeTxn);
  if (dbi) mdb_dbi_close(env, dbi);
  if (env) mdb_env_close(env);
  env = nullptr;
  dbi = 0;
  activeTxn = nullptr;
  { LOCK(cs_db); }
}

bool CDB::Verify() {
  LOCK(cs_db);
  int dead;
  int dbr = mdb_reader_check(env, &dead);
  return dbr;
}

bool CDB::TxnCommit() {
  if (!activeTxn) return false;
  int dbr = mdb_txn_commit(activeTxn);
  activeTxn = nullptr;
  // Get new activeTxn
  // dbr |= mdb_txn_begin(env, nullptr, 0, &activeTxn);
  return (dbr != 0);
}

void CDB::TxnAbort() {
  mdb_txn_abort(activeTxn);
  activeTxn = nullptr;
}

int CDB::ReadAtCursor(MDB_cursor* pcursor, CDataStream& ssKey, CDataStream& ssValue, unsigned int fFlags) {
  // Read at cursor
  MDB_val datKey;
  if (fFlags == MDB_SET || fFlags == MDB_SET_RANGE || fFlags == MDB_GET_BOTH || fFlags == MDB_GET_BOTH_RANGE) {
    datKey.mv_data = &ssKey[0];
    datKey.mv_size = ssKey.size();
  }
  MDB_val datValue;
  if (fFlags == MDB_GET_BOTH || fFlags == MDB_GET_BOTH_RANGE) {
    datValue.mv_data = &ssValue[0];
    datValue.mv_size = ssValue.size();
  }

  int dbr = mdb_cursor_get(pcursor, &datKey, &datValue, MDB_NEXT);

  if (dbr) return dbr;

  // Convert to streams
  ssKey.SetType(SER_DISK);
  ssKey.clear();
  ssKey.write((char*)datKey.mv_data, datKey.mv_size);
  ssValue.SetType(SER_DISK);
  ssValue.clear();
  ssValue.write((char*)datValue.mv_data, datValue.mv_size);
  return 0;
}

bool CDB::Write(CDataStream& key, CDataStream& value, bool fOverwrite) {
  // Key
  MDB_val datKey;
  datKey.mv_data = &key[0];
  datKey.mv_size = key.size();

  // Value
  MDB_val datValue;
  datValue.mv_data = &value[0];
  datValue.mv_size = value.size();

  // Write
  activeTxn = TxnBegin();
  int dbr = mdb_put(activeTxn, dbi, &datKey, &datValue, (fOverwrite ? 0 : MDB_NOOVERWRITE));
  dbr |= TxnCommit();

  // Clear memory in case it was a private key
  memset(datKey.mv_data, 0, datKey.mv_size);
  memset(datValue.mv_data, 0, datValue.mv_size);
  return (dbr == 0);
}
bool CDB::Exists(CDataStream& key) {
  MDB_val datKey;
  datKey.mv_data = &key[0];
  datKey.mv_size = key.size();

  // Exists
  activeTxn = TxnBegin();
  int dbr = mdb_get(activeTxn, dbi, &datKey, nullptr);
  dbr |= TxnCommit();

  // if non-zero, it doesn't exist!
  return (dbr == 0);
}
bool CDB::Erase(CDataStream& key) {
  if (fReadOnly) assert(!"Erase called on database in read-only mode");
  // Key
  MDB_val datKey;
  datKey.mv_data = &key[0];
  datKey.mv_size = key.size();

  // Erase
  activeTxn = TxnBegin();
  int dbr = mdb_del(activeTxn, dbi, &datKey, nullptr);
  dbr |= TxnCommit();
  return (dbr == 0 || dbr == MDB_NOTFOUND);
}
bool CDB::Read(CDataStream& key, CDataStream& value) {
  // Key
  MDB_val datKey;
  datKey.mv_data = &key[0];
  datKey.mv_size = key.size();

  // Read
  MDB_val datValue;
  activeTxn = TxnBegin();
  int dbr = mdb_get(activeTxn, dbi, &datKey, &datValue);
  dbr |= TxnCommit();

  if (dbr) return false;
  // Throw if ret ! = 0!!!!

  try {
    CDataStream ssValue((char*)datValue.mv_data, (char*)datValue.mv_data + datValue.mv_size, SER_DISK, CLIENT_VERSION);
    value = ssValue;
  } catch (const std::exception&) { return false; }
  return (dbr == 0);
}
