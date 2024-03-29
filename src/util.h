// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

/**
 * Server/client environment: argument handling, config file parsing,
 * logging, thread wrappers
 */
#pragma once

#if defined(HAVE_CONFIG_H)
#include "coin-config.h"
#endif

#include "compat.h"
#include "logging.h"
#include "sync.h"
#include "tinyformat.h"

#include <cstdint>
#include <exception>
#include <map>
#include <string>
#include <unordered_set>
#include <vector>

// Tessa only features

extern std::string strMiscWarning;

/**
 * Bypass Translation function: Retrofit if needed to use translation later
 */
inline std::string _(const char* psz) { return psz; }

void SetupEnvironment();
bool SetupNetworking();

template <typename... Args> bool error(const char* fmt, const Args&... args) {
  LogPrintf("ERROR: " + tfm::format(fmt, args...) + "\n");
  return false;
}

double double_safe_addition(double fValue, double fIncrement);
double double_safe_multiplication(double fValue, double fmultiplicator);
void PrintExceptionContinue(std::exception* pex, const char* pszThread);
void ParseParameters(int argc, const char* const argv[]);
void FileCommit(FILE* fileout);
bool TruncateFile(FILE* file, uint32_t length);
int RaiseFileDescriptorLimit(int nMinFD);
void AllocateFileRange(FILE* file, uint32_t offset, uint32_t length);
void ReadConfigFile();
void ShrinkDebugFile();
void runCommand(const std::string& strCommand);

inline bool IsSwitchChar(char c) {
#ifdef WIN32
  return c == '-' || c == '/';
#else
  return c == '-';
#endif
}

/**
 * Return string argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (e.g. "1")
 * @return command-line argument or default value
 */
std::string GetArg(const std::string& strArg, const std::string& strDefault);

/**
 * Return integer argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (e.g. 1)
 * @return command-line argument (0 if invalid number) or default value
 */
int64_t GetArg(const std::string& strArg, int64_t nDefault);

/**
 * Return boolean argument or default value
 *
 * @param strArg Argument to get (e.g. "-foo")
 * @param default (true or false)
 * @return command-line argument or default value
 */
bool GetBoolArg(const std::string& strArg, bool fDefault);

/**
 * Set an argument if it doesn't already have a value
 *
 * @param strArg Argument to set (e.g. "-foo")
 * @param strValue Value (e.g. "1")
 * @return true if argument gets set, false if it already had a value
 */
bool SoftSetArg(const std::string& strArg, const std::string& strValue);

/**
 * Set a boolean argument if it doesn't already have a value
 *
 * @param strArg Argument to set (e.g. "-foo")
 * @param fValue Value (e.g. false)
 * @return true if argument gets set, false if it already had a value
 */
bool SoftSetBoolArg(const std::string& strArg, bool fValue);

/**
 * Format a string to be used as group of options in help messages
 *
 * @param message Group name (e.g. "RPC server options:")
 * @return the formatted string
 */
std::string HelpMessageGroup(const std::string& message);

/**
 * Format a string to be used as option description in help messages
 *
 * @param option Option message (e.g. "-rpcuser=<user>")
 * @param message Option description (e.g. "Username for JSON-RPC connections")
 * @return the formatted string
 */
std::string HelpMessageOpt(const std::string& option, const std::string& message);

void SetThreadPriority(int nPriority);
void RenameThread(const char* name);

class thread_interrupted {};

inline void interruption_point(bool interrupt) {
    if (interrupt) {
        throw thread_interrupted();
    }
}

/**
 * .. and a wrapper that just calls func once
 */
template <typename Callable> void TraceThread(const char* name, Callable func) {
  std::string s = strprintf("tessa-%s", name);
  RenameThread(s.c_str());
  try {
    LogPrintf("%s thread start\n", name);
    func();
    LogPrintf("%s thread exit\n", name);
  }
  catch (thread_interrupted&) {
    LogPrintf("%s thread interrupt\n", name);
  }
  catch (std::exception& e) {
    PrintExceptionContinue(&e, name);
    throw;
  }
  catch (...) {
    PrintExceptionContinue(nullptr, name);
    throw;
  }
}

class ArgsManager {
 protected:
  mutable CCriticalSection cs_args;
  std::map<std::string, std::string> mapArgs;
  std::map<std::string, std::vector<std::string> > mapMultiArgs;
  std::unordered_set<std::string> m_negated_args;

 public:
  void ParseParameters(int argc, const char* const argv[]);
  void ReadConfigFile();

  /**
   * Return a vector of strings of the given argument
   *
   * @param strArg Argument to get (e.g. "-foo")
   * @return command-line arguments
   */
  std::vector<std::string> GetArgs(const std::string& strArg) const;

  /**
   * Return true if the given argument has been manually set
   *
   * @param strArg Argument to get (e.g. "-foo")
   * @return true if the argument has been set
   */
  bool IsArgSet(const std::string& strArg) const;

  /**
   * Return true if the argument was originally passed as a negated option,
   * i.e. -nofoo.
   *
   * @param strArg Argument to get (e.g. "-foo")
   * @return true if the argument was passed negated
   */
  bool IsArgNegated(const std::string& strArg) const;

  /**
   * Return string argument or default value
   *
   * @param strArg Argument to get (e.g. "-foo")
   * @param strDefault (e.g. "1")
   * @return command-line argument or default value
   */
  std::string GetArg(const std::string& strArg, const std::string& strDefault) const;

  /**
   * Return integer argument or default value
   *
   * @param strArg Argument to get (e.g. "-foo")
   * @param nDefault (e.g. 1)
   * @return command-line argument (0 if invalid number) or default value
   */
  int64_t GetArg(const std::string& strArg, int64_t nDefault) const;

  /**
   * Return boolean argument or default value
   *
   * @param strArg Argument to get (e.g. "-foo")
   * @param fDefault (true or false)
   * @return command-line argument or default value
   */
  bool GetBoolArg(const std::string& strArg, bool fDefault) const;

  /**
   * Set an argument if it doesn't already have a value
   *
   * @param strArg Argument to set (e.g. "-foo")
   * @param strValue Value (e.g. "1")
   * @return true if argument gets set, false if it already had a value
   */
  bool SoftSetArg(const std::string& strArg, const std::string& strValue);

  /**
   * Set a boolean argument if it doesn't already have a value
   *
   * @param strArg Argument to set (e.g. "-foo")
   * @param fValue Value (e.g. false)
   * @return true if argument gets set, false if it already had a value
   */
  bool SoftSetBoolArg(const std::string& strArg, bool fValue);

  // Forces an arg setting. Called by SoftSetArg() if the arg hasn't already
  // been set. Also called directly in testing.
  void ForceSetArg(const std::string& strArg, const std::string& strValue);

 private:
  // Munge -nofoo into -foo=0 and track the value as negated.
  void InterpretNegatedOption(std::string& key, std::string& val);
};

extern ArgsManager gArgs;
