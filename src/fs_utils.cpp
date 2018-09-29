// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "fs_utils.h"
#include "chainparamsbase.h"
#include "fs.h"
#include "util.h"

#ifndef WIN32
// for posix_fallocate
#ifdef __linux__

#ifdef _POSIX_C_SOURCE
#undef _POSIX_C_SOURCE
#endif

#define _POSIX_C_SOURCE 200112L

#endif  // __linux__

#include <algorithm>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/stat.h>

#else

#ifdef _MSC_VER
#pragma warning(disable : 4786)
#pragma warning(disable : 4804)
#pragma warning(disable : 4805)
#pragma warning(disable : 4717)
#endif

#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0501

#ifdef _WIN32_IE
#undef _WIN32_IE
#endif
#define _WIN32_IE 0x0501

#define WIN32_LEAN_AND_MEAN 1
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <io.h> /* for _commit */
#include <shlobj.h>
#endif

#ifdef HAVE_SYS_PRCTL_H
#include <sys/prctl.h>
#endif

fs::path GetDefaultDataDir() {
// Windows < Vista: C:\Documents and Settings\Username\Application Data\Tessa
// Windows >= Vista: C:\Users\Username\AppData\Roaming\Tessa
// Mac: ~/Library/Application Support/Tessa
// Unix: ~/.tessa
#ifdef WIN32
  // Windows
  return GetSpecialFolderPath(CSIDL_APPDATA) / "Tessa";
#else
  fs::path pathRet;
  char* pszHome = getenv("HOME");
  if (pszHome == nullptr || strlen(pszHome) == 0)
    pathRet = fs::path("/");
  else
    pathRet = fs::path(pszHome);
#ifdef MAC_OSX
  // Mac
  pathRet /= "Library/Application Support";
  TryCreateDirectory(pathRet);
  return pathRet / "Tessa";
#else
  // Unix
  return pathRet / ".tessa";
#endif
#endif
}

static fs::path pathCached;
static fs::path pathCachedNetSpecific;
static CCriticalSection csPathCached;

void ClearDatadirCache() {
  pathCached = fs::path();
  pathCachedNetSpecific = fs::path();
}

const fs::path& GetDataDir(bool fNetSpecific) {
  LOCK(csPathCached);

  fs::path& path = fNetSpecific ? pathCachedNetSpecific : pathCached;

  // This can be called during exceptions by LogPrintf(), so we cache the
  // value so we don't have to do memory allocations after that.
  if (!path.empty()) return path;

  if (gArgs.IsArgSet("-datadir")) {
    path = fs::system_complete(gArgs.GetArg("-datadir", ""));
    if (!fs::is_directory(path)) {
      path = "";
      return path;
    }
  } else {
    path = GetDefaultDataDir();
  }
  if (fNetSpecific) path = path / BaseParams().DataDir();

  fs::create_directories(path);

  return path;
}

fs::path GetConfigFile() {
  fs::path pathConfigFile(GetArg("-conf", "tessa.conf"));
  if (!pathConfigFile.is_complete()) pathConfigFile = GetDataDir(false) / pathConfigFile;

  return pathConfigFile;
}

#ifndef WIN32
fs::path GetPidFile() {
  fs::path pathPidFile(GetArg("-pid", "tessad.pid"));
  if (!pathPidFile.is_complete()) pathPidFile = GetDataDir() / pathPidFile;
  return pathPidFile;
}

void CreatePidFile(const fs::path& path, pid_t pid) {
  FILE* file = fopen(path.string().c_str(), "w");
  if (file) {
    fprintf(file, "%d\n", pid);
    fclose(file);
  }
}
#endif

bool RenameOver(const fs::path& src, fs::path& dest) {
#ifdef WIN32
  return MoveFileExA(src.string().c_str(), dest.string().c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
#else
  int rc = std::rename(src.string().c_str(), dest.string().c_str());
  return (rc == 0);
#endif /* WIN32 */
}

/**
 * Ignores exceptions thrown by Boost's create_directory if the requested directory exists.
 * Specifically handles case where path p exists, but it wasn't possible for the user to
 * write to the parent directory.
 */
bool TryCreateDirectory(const fs::path& p) {
#ifdef NO_BOOST_FILESYSTEM
  return fs::create_directory(p);
#else
  try {
    return fs::create_directory(p);
  } catch (fs::filesystem_error&) {
    if (!fs::exists(p) || !fs::is_directory(p)) throw;
  }
#endif

  // create_directory didn't create the directory, it had to have existed already
  return false;
}

#ifdef WIN32
fs::path GetSpecialFolderPath(int nFolder, bool fCreate) {
  char pszPath[MAX_PATH] = "";

  if (SHGetSpecialFolderPathA(nullptr, pszPath, nFolder, fCreate)) { return fs::path(pszPath); }

  LogPrintf("SHGetSpecialFolderPathA() failed, could not obtain requested path.\n");
  return fs::path("");
}
#endif

fs::path GetTempPath() { return fs::temp_directory_path(); }
