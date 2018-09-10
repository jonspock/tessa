// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "tessa-config.h"
#endif

#include <cstdarg>
#include <mutex>

#include "chainparamsbase.h"
#include "random.h"
#include "serialize.h"
#include "support/allocators/secure.h"
#include "sync.h"
#include "util.h"
#include "utilstrencodings.h"
#include "utiltime.h"
// Wraps boost::filesystem inside fs namespace (future c++17)
#include "fs.h"
#include "fs_utils.h"

#include <thread>

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

ArgsManager gArgs;

// Tessa only features
/** Spork enforcement enabled time */
bool fSucessfullyLoaded = false;
std::string strMiscWarning;

/** Interpret string as boolean, for argument parsing */
static bool InterpretBool(const std::string& strValue) {
  if (strValue.empty()) return true;
  return (atoi(strValue.c_str()) != 0);
}

/** Turn -noX into -X=0
static void InterpretNegativeSetting(std::string& strKey, std::string& strValue) {
  if (strKey.length() > 3 && strKey[0] == '-' && strKey[1] == 'n' && strKey[2] == 'o') {
    strKey = "-" + strKey.substr(3);
    strValue = InterpretBool(strValue) ? "0" : "1";
  }
}
 */

/* Interpret string as boolean, for argument parsing *
static bool InterpretBool(const std::string& val)
{
    return val != "0";
}
*/

// Treat -nofoo as if the user supplied -foo=0. We also track that this was a
// negated option. This allows non-boolean options to have a "disabled" setting,
// e.g. -nodebuglogfile can be used to disable the -debuglogfile option.
void ArgsManager::InterpretNegatedOption(std::string& key, std::string& val) {
  if (key.substr(0, 3) == "-no") {
    bool bool_val = InterpretBool(val);
    if (!bool_val) {
      // Double negatives like -nofoo=0 are supported (but discouraged)
      LogPrintf("Warning: parsed potentially confusing double-negative %s=%s\n", key, val);
    }
    key.erase(1, 2);
    m_negated_args.insert(key);
    val = bool_val ? "0" : "1";
  } else {
    // In an invocation like "bitcoind -nofoo -foo" we want to unmark -foo
    // as negated when we see the second option.
    m_negated_args.erase(key);
  }
}

void ParseParameters(int argc, const char* const argv[]) { gArgs.ParseParameters(argc, argv); }

std::string GetArg(const std::string& strArg, const std::string& strDefault) {
  return gArgs.GetArg(strArg, strDefault);
}

int64_t GetArg(const std::string& strArg, int64_t nDefault) { return gArgs.GetArg(strArg, nDefault); }

bool GetBoolArg(const std::string& strArg, bool fDefault) { return gArgs.GetBoolArg(strArg, fDefault); }

bool SoftSetArg(const std::string& strArg, const std::string& strValue) { return gArgs.SoftSetArg(strArg, strValue); }

bool SoftSetBoolArg(const std::string& strArg, bool fValue) { return gArgs.SoftSetBoolArg(strArg, fValue); }

static const int screenWidth = 79;
static const int optIndent = 2;
static const int msgIndent = 7;

std::string HelpMessageGroup(const std::string& message) { return std::string(message) + std::string("\n\n"); }

std::string HelpMessageOpt(const std::string& option, const std::string& message) {
  return std::string(optIndent, ' ') + std::string(option) + std::string("\n") + std::string(msgIndent, ' ') +
         FormatParagraph(message, screenWidth - msgIndent, msgIndent) + std::string("\n\n");
}

static std::string FormatException(std::exception* pex, const char* pszThread) {
#ifdef WIN32
  char pszModule[MAX_PATH] = "";
  GetModuleFileNameA(nullptr, pszModule, sizeof(pszModule));
#else
  const char* pszModule = "tessa";
#endif
  if (pex)
    return strprintf("EXCEPTION: %s       \n%s       \n%s in %s       \n", typeid(*pex).name(), pex->what(), pszModule,
                     pszThread);
  else
    return strprintf("UNKNOWN EXCEPTION       \n%s in %s       \n", pszModule, pszThread);
}

void PrintExceptionContinue(std::exception* pex, const char* pszThread) {
  std::string message = FormatException(pex, pszThread);
  LogPrintf("\n\n************************\n%s\n", message);
  fprintf(stderr, "\n\n************************\n%s\n", message.c_str());
  strMiscWarning = message;
}

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
  if (fNetSpecific) path /= BaseParams().DataDir();

  fs::create_directories(path);

  return path;
}

void ClearDatadirCache() {
  pathCached = fs::path();
  pathCachedNetSpecific = fs::path();
}

fs::path GetConfigFile() {
  fs::path pathConfigFile(GetArg("-conf", "tessa.conf"));
  if (!pathConfigFile.is_complete()) pathConfigFile = GetDataDir(false) / pathConfigFile;

  return pathConfigFile;
}

void ReadConfigFile() { gArgs.ReadConfigFile(); }

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
  try {
    return fs::create_directory(p);
  } catch (fs::filesystem_error) {
    if (!fs::exists(p) || !fs::is_directory(p)) throw;
  }

  // create_directory didn't create the directory, it had to have existed already
  return false;
}

void FileCommit(FILE* fileout) {
  fflush(fileout);  // harmless if redundantly called
#ifdef WIN32
  HANDLE hFile = (HANDLE)_get_osfhandle(_fileno(fileout));
  FlushFileBuffers(hFile);
#else
#if defined(__linux__) || defined(__NetBSD__)
  fdatasync(fileno(fileout));
#elif defined(__APPLE__) && defined(F_FULLFSYNC)
  fcntl(fileno(fileout), F_FULLFSYNC, 0);
#else
  fsync(fileno(fileout));
#endif
#endif
}

bool TruncateFile(FILE* file, uint32_t length) {
#if defined(WIN32)
  return _chsize(_fileno(file), length) == 0;
#else
  return ftruncate(fileno(file), length) == 0;
#endif
}

/**
 * this function tries to raise the file descriptor limit to the requested number.
 * It returns the actual file descriptor limit (which may be more or less than nMinFD)
 */
int RaiseFileDescriptorLimit(int nMinFD) {
#if defined(WIN32)
  return 2048;
#else
  struct rlimit limitFD;
  if (getrlimit(RLIMIT_NOFILE, &limitFD) != -1) {
    if (limitFD.rlim_cur < (rlim_t)nMinFD) {
      limitFD.rlim_cur = nMinFD;
      if (limitFD.rlim_cur > limitFD.rlim_max) limitFD.rlim_cur = limitFD.rlim_max;
      setrlimit(RLIMIT_NOFILE, &limitFD);
      getrlimit(RLIMIT_NOFILE, &limitFD);
    }
    return limitFD.rlim_cur;
  }
  return nMinFD;  // getrlimit failed, assume it's fine
#endif
}

/**
 * this function tries to make a particular range of a file allocated (corresponding to disk space)
 * it is advisory, and the range specified in the arguments will never contain live data
 */
void AllocateFileRange(FILE* file, uint32_t offset, uint32_t length) {
#if defined(WIN32)
  // Windows-specific version
  HANDLE hFile = (HANDLE)_get_osfhandle(_fileno(file));
  LARGE_INTEGER nFileSize;
  int64_t nEndPos = (int64_t)offset + length;
  nFileSize.u.LowPart = nEndPos & 0xFFFFFFFF;
  nFileSize.u.HighPart = nEndPos >> 32;
  SetFilePointerEx(hFile, nFileSize, 0, FILE_BEGIN);
  SetEndOfFile(hFile);
#elif defined(MAC_OSX)
  // OSX specific version
  fstore_t fst;
  fst.fst_flags = F_ALLOCATECONTIG;
  fst.fst_posmode = F_PEOFPOSMODE;
  fst.fst_offset = 0;
  fst.fst_length = (off_t)offset + length;
  fst.fst_bytesalloc = 0;
  if (fcntl(fileno(file), F_PREALLOCATE, &fst) == -1) {
    fst.fst_flags = F_ALLOCATEALL;
    fcntl(fileno(file), F_PREALLOCATE, &fst);
  }
  ftruncate(fileno(file), fst.fst_length);
#elif defined(__linux__)
  // Version using posix_fallocate
  off_t nEndPos = (off_t)offset + length;
  posix_fallocate(fileno(file), 0, nEndPos);
#else
  // Fallback version
  // TODO: just write one byte per block
  static const char buf[65536] = {};
  fseek(file, offset, SEEK_SET);
  while (length > 0) {
    uint32_t now = 65536;
    if (length < now) now = length;
    fwrite(buf, 1, now, file);  // allowed to fail; this function is advisory anyway
    length -= now;
  }
#endif
}

void ShrinkDebugFile() {
  // Scroll debug.log if it's getting too big
  fs::path pathLog = GetDataDir() / "debug.log";
  FILE* file = fopen(pathLog.string().c_str(), "r");
  if (file && fs::file_size(pathLog) > 10 * 1000000) {
    // Restart the file with some of the end
    std::vector<char> vch(200000, 0);
    fseek(file, -((int64_t)vch.size()), SEEK_END);
    int nBytes = fread(begin_ptr(vch), 1, vch.size(), file);
    fclose(file);

    file = fopen(pathLog.string().c_str(), "w");
    if (file) {
      fwrite(begin_ptr(vch), 1, nBytes, file);
      fclose(file);
    }
  } else if (file != nullptr)
    fclose(file);
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

double double_safe_addition(double fValue, double fIncrement) {
  double fLimit = std::numeric_limits<double>::max() - fValue;

  if (fLimit > fIncrement)
    return fValue + fIncrement;
  else
    return std::numeric_limits<double>::max();
}

double double_safe_multiplication(double fValue, double fmultiplicator) {
  double fLimit = std::numeric_limits<double>::max() / fmultiplicator;

  if (fLimit > fmultiplicator)
    return fValue * fmultiplicator;
  else
    return std::numeric_limits<double>::max();
}

void runCommand(const std::string& strCommand) {
  int nErr = ::system(strCommand.c_str());
  if (nErr) LogPrintf("runCommand error: system(%s) returned %d\n", strCommand, nErr);
}

void RenameThread(const char* name) {
#if defined(PR_SET_NAME)
  // Only the first 15 characters are used (16 - NUL terminator)
  ::prctl(PR_SET_NAME, name, 0, 0, 0);
#elif 0 && (defined(__FreeBSD__) || defined(__OpenBSD__))
  // TODO: This is currently disabled because it needs to be verified to work
  //       on FreeBSD or OpenBSD first. When verified the '0 &&' part can be
  //       removed.
  pthread_set_name_np(pthread_self(), name);

#elif defined(MAC_OSX) && defined(__MAC_OS_X_VERSION_MAX_ALLOWED)

// pthread_setname_np is XCode 10.6-and-later
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 1060
  pthread_setname_np(name);
#endif

#else
  // Prevent warnings for unused parameters...
  (void)name;
#endif
}

void SetupEnvironment() {
// On most POSIX systems (e.g. Linux, but not BSD) the environment's locale
// may be invalid, in which case the "C" locale is used as fallback.
#if !defined(WIN32) && !defined(MAC_OSX) && !defined(__FreeBSD__) && !defined(__OpenBSD__)
  try {
    std::locale("");  // Raises a runtime error if current locale is invalid
  } catch (const std::runtime_error&) { setenv("LC_ALL", "C", 1); }
#endif
  // The path locale is lazy initialized and to avoid deinitialization errors
  // in multithreading environments, it is set explicitly by the main thread.
  // A dummy locale is used to extract the internal default locale, used by
  // fs::path, which is then used to explicitly imbue the path.
  std::locale loc = fs::path::imbue(std::locale::classic());
  fs::path::imbue(loc);
}

bool SetupNetworking() {
#ifdef WIN32
  // Initialize Windows Sockets
  WSADATA wsadata;
  int ret = WSAStartup(MAKEWORD(2, 2), &wsadata);
  if (ret != NO_ERROR || LOBYTE(wsadata.wVersion) != 2 || HIBYTE(wsadata.wVersion) != 2) return false;
#endif
  return true;
}

void SetThreadPriority(int nPriority) {
#ifdef WIN32
  SetThreadPriority(GetCurrentThread(), nPriority);
#else  // WIN32
#ifdef PRIO_THREAD
  setpriority(PRIO_THREAD, 0, nPriority);
#else   // PRIO_THREAD
  setpriority(PRIO_PROCESS, 0, nPriority);
#endif  // PRIO_THREAD
#endif  // WIN32
}

inline bool not_space(int c) { return !std::isspace(c); }

void ArgsManager::ReadConfigFile() {
  fs::ifstream config_file(GetConfigFile());
  if (!config_file.good()) return;  // No bitcoin.conf file is OK

  {
    // left and right trim for strings
    auto ltrim = [](std::string& s) { s.erase(s.begin(), std::find_if(s.begin(), s.end(), not_space)); };
    auto rtrim = [](std::string& s) { s.erase(std::find_if(s.rbegin(), s.rend(), not_space).base(), s.end()); };

    LOCK(cs_args);
    std::string line;
    while (std::getline(config_file, line)) {
      size_t eqpos = line.find('=');
      std::string key, val;
      if (eqpos == std::string::npos) {
        key = line;
      } else {
        key = line.substr(0, eqpos);
        val = line.substr(eqpos + 1);
      }

      // trim whitespace on the key and value
      ltrim(key);
      rtrim(key);
      ltrim(val);
      rtrim(val);

      // convert to cli argument form
      key = "-" + key;

      // handle -nofoo options
      InterpretNegatedOption(key, val);

      if (mapArgs.count(key) == 0) mapArgs[key] = val;
      mapMultiArgs[key].push_back(val);
    }
  }
  // If datadir is changed in .conf file:
  ClearDatadirCache();
  if (!fs::is_directory(GetDataDir(false))) {
    throw std::runtime_error(
        strprintf("specified data directory \"%s\" does not exist.", gArgs.GetArg("-datadir", "").c_str()));
  }
}

void ArgsManager::ParseParameters(int argc, const char* const argv[]) {
  LOCK(cs_args);
  mapArgs.clear();
  mapMultiArgs.clear();
  m_negated_args.clear();

  for (int i = 1; i < argc; i++) {
    std::string key(argv[i]);
    std::string val;
    size_t is_index = key.find('=');
    if (is_index != std::string::npos) {
      val = key.substr(is_index + 1);
      key.erase(is_index);
    }
#ifdef WIN32
    std::transform(key.begin(), key.end(), key.begin(), ::tolower);
    if (key[0] == '/') key[0] = '-';
#endif

    if (key[0] != '-') break;

    // Transform --foo to -foo
    if (key.length() > 1 && key[1] == '-') key.erase(0, 1);

    // Transform -nofoo to -foo=0
    InterpretNegatedOption(key, val);

    mapArgs[key] = val;
    mapMultiArgs[key].push_back(val);
  }
}

std::vector<std::string> ArgsManager::GetArgs(const std::string& strArg) const {
  LOCK(cs_args);
  auto it = mapMultiArgs.find(strArg);
  if (it != mapMultiArgs.end()) return it->second;
  return {};
}

bool ArgsManager::IsArgSet(const std::string& strArg) const {
  LOCK(cs_args);
  return mapArgs.count(strArg);
}

bool ArgsManager::IsArgNegated(const std::string& strArg) const {
  LOCK(cs_args);
  return m_negated_args.find(strArg) != m_negated_args.end();
}

std::string ArgsManager::GetArg(const std::string& strArg, const std::string& strDefault) const {
  LOCK(cs_args);
  auto it = mapArgs.find(strArg);
  if (it != mapArgs.end()) return it->second;
  return strDefault;
}

int64_t ArgsManager::GetArg(const std::string& strArg, int64_t nDefault) const {
  LOCK(cs_args);
  auto it = mapArgs.find(strArg);
  if (it != mapArgs.end()) return std::atoi(it->second.c_str());
  return nDefault;
}

bool ArgsManager::GetBoolArg(const std::string& strArg, bool fDefault) const {
  LOCK(cs_args);
  auto it = mapArgs.find(strArg);
  if (it != mapArgs.end()) return InterpretBool(it->second);
  return fDefault;
}

bool ArgsManager::SoftSetArg(const std::string& strArg, const std::string& strValue) {
  LOCK(cs_args);
  if (IsArgSet(strArg)) return false;
  ForceSetArg(strArg, strValue);
  return true;
}

bool ArgsManager::SoftSetBoolArg(const std::string& strArg, bool fValue) {
  if (fValue)
    return SoftSetArg(strArg, std::string("1"));
  else
    return SoftSetArg(strArg, std::string("0"));
}

void ArgsManager::ForceSetArg(const std::string& strArg, const std::string& strValue) {
  LOCK(cs_args);
  mapArgs[strArg] = strValue;
  mapMultiArgs[strArg] = {strValue};
}
