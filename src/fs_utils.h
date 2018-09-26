// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once


#ifdef WIN32
#include <mswsock.h>
#include <windows.h>
#include <winsock2.h>  // Must be included before mswsock.h and windows.h
#include <ws2tcpip.h>
#else
#include <sys/types.h>
#endif

#ifdef NO_BOOST_FILESYSTEM
#include "fs.h"
#else
// Forward decl
namespace boost {
  namespace filesystem {
    class path;
  }
}  // namespace boost
namespace fs = boost::filesystem;
#endif

struct CDiskBlockPos;

fs::path GetDefaultDataDir();
const fs::path& GetDataDir(bool fNetSpecific = true);
fs::path GetConfigFile();
#ifndef WIN32
fs::path GetPidFile();
void CreatePidFile(const fs::path& path, pid_t pid);
#endif
#ifdef WIN32
fs::path GetSpecialFolderPath(int nFolder, bool fCreate = true);
#endif
fs::path GetTempPath();
bool TryCreateDirectory(const fs::path& p);
bool RenameOver(const fs::path& src, fs::path& dest);
void ClearDatadirCache();

/** Translation to a filesystem path */
fs::path GetBlockPosFilename(const CDiskBlockPos& pos, const char* prefix);
