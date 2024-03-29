// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#if defined(HAVE_CONFIG_H)
#include "coin-config.h"
#endif

#include "init.h"

#include "addrman.h"
#include "amount.h"
#include "checkpoints.h"
#include "compat/sanity.h"
#include "ecdsa/key.h"
#include "fs.h"
#include "fs_utils.h"
#include "httprpc.h"
#include "httpserver.h"
#include "main.h"
#include "miner.h"
#include "net.h"
#include "reverse_iterate.h"
#include "rpc/server.h"
#include "scheduler.h"
#include "script/standard.h"
#include "spork/spork.h"
#include "spork/sporkdb.h"
#include "txdb.h"
#include "ui_interface.h"
#include "util.h"
#include "utilmoneystr.h"
#include "utiltime.h"
#include "validationinterface.h"
#include "validationstate.h"
#include "verifydb.h"
#include "zerocoin/accumulatorcheckpoints.h"
#include "zerocoin/zerochain.h"
#include "zerocoin/zerocoindb.h"

#include "ecdsa/ecdsa.h"
#include "wallet/db.h"
#include "wallet/wallet.h"
#include "wallet/walletdb.h"
#include "wallet/wallettx.h"
#include "zerocoin/accumulators.h"

#include <cstdint>
#include <fstream>
#include <thread>

#ifndef WIN32
#include <signal.h>
// for umask
#include <sys/stat.h>
#include <sys/types.h>
#endif

#ifndef NO_BOOST_FILESYSTEM
#include <boost/interprocess/sync/file_lock.hpp>
#include <boost/thread/thread_time.hpp>
#endif

#if ENABLE_ZMQ
#include "zmq/zmqnotificationinterface.h"
#endif

#include <sodium/core.h>

using namespace std;
using fs::create_directories;
using fs::exists;
using fs::path;
#ifndef NO_BOOST_FILESYSTEM
using fs::filesystem_error;
#endif

CWallet* pwalletMain = nullptr;
CZeroWallet* zwalletMain = nullptr;
const int nWalletBackups = 10;
// Specific to LMDB, may have to change some related code otherwise
const std::string strWalletFile = "data.mdb";

volatile bool fRestartRequested = false;  // true: restart false: shutdown
extern std::list<uint256> listAccCheckpointsNoDB;
static bool fDisableWallet = false;

#if ENABLE_ZMQ
static CZMQNotificationInterface* pzmqNotificationInterface = nullptr;
#endif

#ifdef WIN32
// Win32 LevelDB doesn't use filedescriptors, and the ones used for
// accessing block files, don't count towards to fd_set size limit
// anyway.
#define MIN_CORE_FILEDESCRIPTORS 0
#else
#define MIN_CORE_FILEDESCRIPTORS 150
#endif

/** Used to pass flags to the Bind() function */
enum BindFlags {
  BF_NONE = 0,
  BF_EXPLICIT = (1U << 0),
  BF_REPORT_ERROR = (1U << 1),
  BF_WHITELIST = (1U << 2),
};

CClientUIInterface uiInterface;

//////////////////////////////////////////////////////////////////////////////
//
// Shutdown
//

//
// Thread management and startup/shutdown:
//
// The network-processing threads are all part of a thread group
// created by AppInit() or the Qt main() function.
//
// A clean exit happens when StartShutdown() or the SIGTERM
// signal handler sets fRequestShutdown, which triggers
// the DetectShutdownThread(), which interrupts the main thread group.
// DetectShutdownThread() then exits, which causes AppInit() to
// continue (it .joins the shutdown thread).
// Shutdown() is then
// called to clean up database connections, and stop other
// threads that should only be stopped after the main network-processing
// threads have exited.
//
// Note that if running -daemon the parent process returns from AppInit2
// before adding any threads to the threadGroup, so .join_all() returns
// immediately and the parent exits from main().
//
// Shutdown for Qt is very similar, only it uses a QTimer to detect
// fRequestShutdown getting set, and then does the normal Qt
// shutdown thing.
//

volatile bool fRequestShutdown = false;

bool WalletDisabled() { return fDisableWallet; }
void StartShutdown() { fRequestShutdown = true; }
bool ShutdownRequested() { return fRequestShutdown || fRestartRequested; }

class CCoinsViewErrorCatcher : public CCoinsViewBacked {
 public:
  CCoinsViewErrorCatcher(CCoinsView* view) : CCoinsViewBacked(view) {}
  bool GetCoins(const uint256& txid, CCoins& coins) const {
    try {
      return CCoinsViewBacked::GetCoins(txid, coins);
    } catch (const std::runtime_error& e) {
      bool fRet;
      uiInterface.ThreadSafeMessageBox.fire(_("Error reading from database, shutting down."), "",
                                            CClientUIInterface::MSG_ERROR, &fRet);
      LogPrintf("Error reading from database: %s\n", e.what());
      // Starting the shutdown sequence and returning false to the caller would be
      // interpreted as 'entry not found' (as opposed to unable to read data), and
      // could lead to invalid interpration. Just exit immediately, as we can't
      // continue anyway, and all writes should be atomic.
      abort();
    }
  }
  // Writes do not need similar protection, as failure to write is handled by the caller.
};

static CCoinsViewDB* pcoinsdbview = nullptr;
static CCoinsViewErrorCatcher* pcoinscatcher = nullptr;
static std::unique_ptr<ECCVerifyHandle> globalVerifyHandle;

static std::thread scheduler_thread;
static std::vector<std::thread> script_check_threads;
static std::thread import_thread;

static uint32_t nCoinCacheSize = 5000;

uint32_t getCoinCacheSize() { return nCoinCacheSize; }

void Interrupt(CScheduler& scheduler) {
  InterruptHTTPServer();
  InterruptHTTPRPC();
  InterruptRPC();
  InterruptREST();
  InterruptMapPort();
  scheduler.interrupt(false);
  if (gpBlockTreeDB) gpBlockTreeDB->InterruptLoadBlockIndexGuts();
  /// HACK TBD!!!!
  // CVerifyDB().InterruptInit();
  pcoinsdbview->InterruptGetStats();
  gpZerocoinDB->InterruptWipeCoins();

  InterruptThreadScriptCheck();
  InterruptNetBase();
  InterruptNode();
  InterruptMiner();
  //  InterruptWallet();
  InterruptSearch();
}

/** Preparing steps before shutting down or restarting the wallet */
void PrepareShutdown(CScheduler& scheduler) {
  fRequestShutdown = true;   // Needed when we shutdown the wallet
  fRestartRequested = true;  // Needed when we restart the wallet
  LogPrintf("%s: In progress...\n", __func__);
  static CCriticalSection cs_Shutdown;
  TRY_LOCK(cs_Shutdown, lockShutdown);
  if (!lockShutdown) return;

  /// Note: Shutdown() must be able to handle cases in which AppInit2() failed part of the way,
  /// for example if the data directory was found to be locked.
  /// Be sure that anything that writes files or flushes caches only does this if the respective
  /// module was initialized.
  RenameThread("tessa-shutoff");
  mempool.AddTransactionsUpdated(1);
  StopHTTPRPC();
  StopREST();
  StopRPC();
  StopHTTPServer();

  GenerateBitcoins(false, nullptr, 0);

  StopNode();

  StopMapPort();
  scheduler.stop();
  if (scheduler_thread.joinable()) scheduler_thread.join();
  for (auto&& thread : script_check_threads) thread.join();
  script_check_threads.clear();
  if (import_thread.joinable()) import_thread.join();

  UnregisterNodeSignals(GetNodeSignals());

  {
    LOCK(cs_main);
    if (gpCoinsTip != nullptr) {
      FlushStateToDisk();

      // record that client took the proper shutdown procedure
      gpBlockTreeDB->WriteFlag("shutdown", true);
    }
    if (gpCoinsTip) delete gpCoinsTip;
    if (pcoinscatcher) delete pcoinscatcher;
    if (pcoinsdbview) delete pcoinsdbview;
    gpBlockTreeDB.reset(nullptr);
    gpZerocoinDB.reset(nullptr);
  }

#if ENABLE_ZMQ
  if (pzmqNotificationInterface) {
    UnregisterValidationInterface(pzmqNotificationInterface);
    delete pzmqNotificationInterface;
    pzmqNotificationInterface = nullptr;
  }
#endif

#ifndef WIN32
#ifdef NO_BOOST_FILESYSTEM
  fs::remove(GetPidFile());
#else
  try {
    fs::remove(GetPidFile());
  } catch (const fs::filesystem_error& e) { LogPrintf("%s: Unable to remove pidfile: %s\n", __func__, e.what()); }
#endif
#endif
  UnregisterAllValidationInterfaces();
}

/**
 * Shutdown is split into 2 parts:
 * Part 1: shut down everything but the main wallet instance (done in PrepareShutdown() )
 * Part 2: delete wallet instance
 *
 * In case of a restart PrepareShutdown() was already called before, but this method here gets
 * called implicitly when the parent object is deleted. In this case we have to skip the
 * PrepareShutdown() part because it was already executed and just delete the wallet instance.
 */
void Shutdown(CScheduler& scheduler) {
  // Shutdown part 1: prepare shutdown
  if (!fRestartRequested) { PrepareShutdown(scheduler); }
  // Shutdown part 2: Stop TOR thread and delete wallet instance

  if (pwalletMain) delete pwalletMain;
  pwalletMain = nullptr;
  if (zwalletMain) delete zwalletMain;
  zwalletMain = nullptr;
  globalVerifyHandle.reset();
  ECC_Stop();
  LogPrintf("%s: done\n", __func__);
}

/**
 * Signal handlers are very limited in what they are allowed to do, so:
 */
void HandleSIGTERM(int) { fRequestShutdown = true; }

void HandleSIGHUP(int) { GetLogger().fReopenDebugLog = true; }

bool static InitError(const std::string& str) {
  bool fRet;
  uiInterface.ThreadSafeMessageBox.fire(str, "", CClientUIInterface::MSG_ERROR, &fRet);
  return false;
}

bool static InitWarning(const std::string& str) {
  bool fRet;
  uiInterface.ThreadSafeMessageBox.fire(str, "", CClientUIInterface::MSG_WARNING, &fRet);
  return true;
}

bool static Bind(const CService& addr, uint32_t flags) {
  if (!(flags & BF_EXPLICIT) && IsLimited(addr)) return false;
  std::string strError;
  if (!BindListenPort(addr, strError, (flags & BF_WHITELIST) != 0)) {
    if (flags & BF_REPORT_ERROR) return InitError(strError);
    return false;
  }
  return true;
}

void OnRPCStopped() {
  cvBlockChange.notify_all();
  LogPrint(TessaLog::RPC, "RPC stopped.\n");
}

void OnRPCPreCommand(const CRPCCommand& cmd) {
  fDisableWallet = GetBoolArg("-disablewallet", false);
  if (!fDisableWallet)
    if (cmd.reqWallet && !pwalletMain) throw JSONRPCError(RPC_METHOD_NOT_FOUND, "Method not found (disabled)");

  // Observe safe mode
  string strWarning = GetWarnings("rpc");
  if (strWarning != "" && !GetBoolArg("-disablesafemode", false) && !cmd.okSafeMode)
    throw JSONRPCError(RPC_FORBIDDEN_BY_SAFE_MODE, string("Safe mode: ") + strWarning);
}

std::string HelpMessage(HelpMessageMode mode) {
  // When adding new options to the categories, please keep and ensure alphabetical ordering.
  string strUsage = HelpMessageGroup(_("Options:"));
  strUsage += HelpMessageOpt("-?", _("This help message"));
  strUsage += HelpMessageOpt("-version", _("Print version and exit"));
  strUsage += HelpMessageOpt("-alertnotify=<cmd>",
                             _("Execute command when a we see a really long fork (%s in cmd is replaced by message)"));
  strUsage += HelpMessageOpt("-blocknotify=<cmd>",
                             _("Execute command when the best block changes (%s in cmd is replaced by block hash)"));
  strUsage +=
      HelpMessageOpt("-blocksizenotify=<cmd>", _("Execute command when the best block changes and its size is over (%s "
                                                 "in cmd is replaced by block hash, %d with the block size)"));
  strUsage += HelpMessageOpt("-checkblocks=<n>",
                             strprintf(_("How many blocks to check at startup (default: %u, 0 = all)"), 500));
  strUsage += HelpMessageOpt("-conf=<file>", strprintf(_("Specify configuration file (default: %s)"), "tessa.conf"));
  if (mode == HMM_BITCOIND) {
#if !defined(WIN32)
    strUsage += HelpMessageOpt("-daemon", _("Run in the background as a daemon and accept commands"));
#endif
  }
  strUsage += HelpMessageOpt("-datadir=<dir>", _("Specify data directory"));
  strUsage +=
      HelpMessageOpt("-dbcache=<n>", strprintf(_("Set database cache size in megabytes (%d to %d, default: %d)"),
                                               nMinDbCache, nMaxDbCache, nDefaultDbCache));
  strUsage +=
      HelpMessageOpt("-loadblock=<file>", _("Imports blocks from external blk000??.dat file") + " " + _("on startup"));
  strUsage += HelpMessageOpt("-maxreorg=<n>", strprintf(_("Set the Maximum reorg depth (default: %u)"),
                                                        Params(CBaseChainParams::MAIN).MaxReorganizationDepth()));
  strUsage += HelpMessageOpt("-maxorphantx=<n>",
                             strprintf(_("Keep at most <n> unconnectable transactions in memory (default: %u)"),
                                       DEFAULT_MAX_ORPHAN_TRANSACTIONS));
  strUsage += HelpMessageOpt("-par=<n>", strprintf(_("Set the number of script verification threads (%u to %d, 0 = "
                                                     "auto, <0 = leave that many cores free, default: %d)"),
                                                   -(int)std::thread::hardware_concurrency(), MAX_SCRIPTCHECK_THREADS,
                                                   DEFAULT_SCRIPTCHECK_THREADS));
#ifndef WIN32
  strUsage += HelpMessageOpt("-pid=<file>", strprintf(_("Specify pid file (default: %s)"), "tessad.pid"));
#endif
  strUsage += HelpMessageOpt("-reindex",
                             _("Rebuild block chain index from current blk000??.dat files") + " " + _("on startup"));
  strUsage += HelpMessageOpt("-reindexaccumulators", _("Reindex the accumulator database") + " " + _("on startup"));
  strUsage += HelpMessageOpt("-resync", _("Delete blockchain folders and resync from scratch") + " " + _("on startup"));
#if !defined(WIN32)
  strUsage += HelpMessageOpt("-sysperms", _("Create new files with system default permissions, instead of umask 077 "
                                            "(only effective with disabled wallet functionality)"));
#endif
  strUsage += HelpMessageOpt(
      "-txindex",
      strprintf(_("Maintain a full transaction index, used by the getrawtransaction rpc call (default: %u)"), 0));
  strUsage +=
      HelpMessageOpt("-forcestart", _("Attempt to force blockchain corruption recovery") + " " + _("on startup"));

  strUsage += HelpMessageGroup(_("Connection options:"));
  strUsage += HelpMessageOpt("-addnode=<ip>", _("Add a node to connect to and attempt to keep the connection open"));
  strUsage +=
      HelpMessageOpt("-banscore=<n>", strprintf(_("Threshold for disconnecting misbehaving peers (default: %u)"), 100));
  strUsage += HelpMessageOpt(
      "-bantime=<n>",
      strprintf(_("Number of seconds to keep misbehaving peers from reconnecting (default: %u)"), 86400));
  strUsage += HelpMessageOpt("-bind=<addr>",
                             _("Bind to given address and always listen on it. Use [host]:port notation for IPv6"));
  strUsage += HelpMessageOpt("-connect=<ip>", _("Connect only to the specified node(s)"));
  strUsage += HelpMessageOpt("-discover", _("Discover own IP address (default: 1 when listening and no -externalip)"));
  strUsage +=
      HelpMessageOpt("-dns", _("Allow DNS lookups for -addnode, -seednode and -connect") + " " + _("(default: 1)"));
  strUsage += HelpMessageOpt(
      "-dnsseed", _("Query for peer addresses via DNS lookup, if low on addresses (default: 1 unless -connect)"));
  strUsage += HelpMessageOpt("-externalip=<ip>", _("Specify your own public address"));
  strUsage +=
      HelpMessageOpt("-forcednsseed", strprintf(_("Always query for peer addresses via DNS lookup (default: %u)"), 0));
  strUsage += HelpMessageOpt("-listen", _("Accept connections from outside (default: 1 if no -proxy or -connect)"));
  strUsage += HelpMessageOpt("-maxconnections=<n>",
                             strprintf(_("Maintain at most <n> connections to peers (default: %u)"), 125));
  strUsage += HelpMessageOpt("-maxreceivebuffer=<n>",
                             strprintf(_("Maximum per-connection receive buffer, <n>*1000 bytes (default: %u)"), 5000));
  strUsage += HelpMessageOpt("-maxsendbuffer=<n>",
                             strprintf(_("Maximum per-connection send buffer, <n>*1000 bytes (default: %u)"), 1000));
  strUsage += HelpMessageOpt(
      "-onion=<ip:port>",
      strprintf(_("Use separate SOCKS5 proxy to reach peers via Tor hidden services (default: %s)"), "-proxy"));
  strUsage += HelpMessageOpt("-onlynet=<net>", _("Only connect to nodes in network <net> (ipv4, ipv6 or onion)"));
  strUsage += HelpMessageOpt("-permitbaremultisig", strprintf(_("Relay non-P2SH multisig (default: %u)"), 1));
  strUsage += HelpMessageOpt(
      "-peerbloomfilters", strprintf(_("Support filtering of blocks and transaction with bloom filters (default: %u)"),
                                     DEFAULT_PEERBLOOMFILTERS));
  strUsage += HelpMessageOpt(
      "-port=<port>", strprintf(_("Listen for connections on <port> (default: %u or testnet: %u)"), 44444, 44446));
  strUsage += HelpMessageOpt("-proxy=<ip:port>", _("Connect through SOCKS5 proxy"));
  strUsage += HelpMessageOpt(
      "-proxyrandomize",
      strprintf(_("Randomize credentials for every proxy connection. This enables Tor stream isolation (default: %u)"),
                1));
  strUsage += HelpMessageOpt("-seednode=<ip>", _("Connect to a node to retrieve peer addresses, and disconnect"));
  strUsage += HelpMessageOpt(
      "-timeout=<n>",
      strprintf(_("Specify connection timeout in milliseconds (minimum: 1, default: %d)"), DEFAULT_CONNECT_TIMEOUT));
#ifdef USE_UPNP
#if USE_UPNP
  strUsage += HelpMessageOpt("-upnp", _("Use UPnP to map the listening port (default: 1 when listening)"));
#else
  strUsage += HelpMessageOpt("-upnp", strprintf(_("Use UPnP to map the listening port (default: %u)"), 0));
#endif
#endif
  strUsage += HelpMessageOpt(
      "-whitebind=<addr>",
      _("Bind to given address and whitelist peers connecting to it. Use [host]:port notation for IPv6"));
  strUsage += HelpMessageOpt(
      "-whitelist=<netmask>",
      _("Whitelist peers connecting from the given netmask or IP address. Can be specified multiple times.") + " " +
          _("Whitelisted peers cannot be DoS banned and their transactions are always relayed, even if they are "
            "already in the mempool, useful e.g. for a gateway"));

  if (!fDisableWallet) {
    strUsage += HelpMessageGroup(_("Wallet options:"));
    strUsage +=
        HelpMessageOpt("-backuppath=<dir|file>",
                       _("Specify custom backup path to add a copy of any wallet backup. If set as dir, every backup "
                         "generates a timestamped file. If set as file, will rewrite to that file every backup."));
    strUsage += HelpMessageOpt("-createwalletbackups=<n>", _("Number of automatic wallet backups (default: 10)"));
    strUsage += HelpMessageOpt(
        "-custombackupthreshold=<n>",
        strprintf(_("Number of custom location backups to retain (default: %d)"), DEFAULT_CUSTOMBACKUPTHRESHOLD));
    strUsage += HelpMessageOpt("-disablewallet", _("Do not load the wallet and disable wallet RPC calls"));
    strUsage += HelpMessageOpt("-keypool=<n>", strprintf(_("Set key pool size to <n> (default: %u)"), 100));
    strUsage +=
        HelpMessageOpt("-rescan", _("Rescan the block chain for missing wallet transactions") + " " + _("on startup"));
    strUsage += HelpMessageOpt("-sendfreetransactions",
                               strprintf(_("Send transactions as zero-fee transactions if possible (default: %u)"), 0));
    strUsage += HelpMessageOpt("-spendzeroconfchange",
                               strprintf(_("Spend unconfirmed change when sending transactions (default: %u)"), 1));
    strUsage += HelpMessageOpt("-disablesystemnotifications",
                               strprintf(_("Disable OS notifications for incoming transactions (default: %u)"), 0));
    strUsage += HelpMessageOpt("-txconfirmtarget=<n>",
                               strprintf(_("Include enough fee so transactions "
                                           "begin confirmation on average within n blocks (default: %u)"),
                                         1));
    strUsage += HelpMessageOpt("-wallet=<file>", _("Specify wallet directory (within data directory)") + " " +
                                                     strprintf(_("(default: %s)"), "wallet"));
    strUsage += HelpMessageOpt("-walletnotify=<cmd>",
                               _("Execute command when a wallet transaction changes (%s in cmd is replaced by TxID)"));
    if (mode == HMM_BITCOIN_QT) strUsage += HelpMessageOpt("-windowtitle=<name>", _("Wallet window title"));
    strUsage += HelpMessageOpt(
        "-zapwallettxes=<mode>",
        _("Delete all wallet transactions and only recover those parts of the blockchain through -rescan on startup") +
            " " +
            _("(1 = keep tx meta data e.g. account owner and payment request information, 2 = drop tx meta data)"));
  }

#if ENABLE_ZMQ
  strUsage += HelpMessageGroup(_("ZeroMQ notification options:"));
  strUsage += HelpMessageOpt("-zmqpubhashblock=<address>", _("Enable publish hash block in <address>"));
  strUsage += HelpMessageOpt("-zmqpubhashtx=<address>", _("Enable publish hash transaction in <address>"));
  strUsage += HelpMessageOpt("-zmqpubhashtxlock=<address>",
                             _("Enable publish hash transaction (locked via SwiftX) in <address>"));
  strUsage += HelpMessageOpt("-zmqpubrawblock=<address>", _("Enable publish raw block in <address>"));
  strUsage += HelpMessageOpt("-zmqpubrawtx=<address>", _("Enable publish raw transaction in <address>"));
  strUsage += HelpMessageOpt("-zmqpubrawtxlock=<address>",
                             _("Enable publish raw transaction (locked via SwiftX) in <address>"));
#endif

  strUsage += HelpMessageGroup(_("Debugging/Testing options:"));
  if (GetBoolArg("-help-debug", false)) {
    strUsage +=
        HelpMessageOpt("-checkblockindex",
                       strprintf("Do a full consistency check for mapBlockIndex, setBlockIndexCandidates, chainActive "
                                 "and mapBlocksUnlinked occasionally. Also sets -checkmempool (default: %u)",
                                 Params(CBaseChainParams::MAIN).DefaultConsistencyChecks()));
    strUsage +=
        HelpMessageOpt("-checkmempool=<n>", strprintf("Run checks every <n> transactions (default: %u)",
                                                      Params(CBaseChainParams::MAIN).DefaultConsistencyChecks()));
    strUsage += HelpMessageOpt("-checkpoints",
                               strprintf(_("Only accept block chain matching built-in checkpoints (default: %u)"), 1));
    strUsage += HelpMessageOpt(
        "-dblogsize=<n>",
        strprintf(_("Flush database activity from memory pool to disk log every <n> megabytes (default: %u)"), 100));
    strUsage += HelpMessageOpt("-disablesafemode",
                               strprintf(_("Disable safemode, override a real safe mode event (default: %u)"), 0));
    strUsage += HelpMessageOpt("-testsafemode", strprintf(_("Force safe mode (default: %u)"), 0));
    strUsage += HelpMessageOpt("-dropmessagestest=<n>", _("Randomly drop 1 of every <n> network messages"));
    strUsage += HelpMessageOpt("-fuzzmessagestest=<n>", _("Randomly fuzz 1 of every <n> network messages"));
    strUsage +=
        HelpMessageOpt("-maxreorg", strprintf(_("Use a custom max chain reorganization depth (default: %u)"), 100));
    strUsage += HelpMessageOpt("-stopafterblockimport",
                               strprintf(_("Stop running after importing blocks from disk (default: %u)"), 0));
    strUsage += HelpMessageOpt("-sporkkey=<privkey>",
                               _("Enable spork administration functionality with the appropriate private key."));
  }
  string debugCategories =
      "addrman, alert, bench, coindb, db, lock, rand, rpc, selectcoins, tor, mempool, net, proxy, http, libevent, "
      "tessa, zero, spork, miner)";  // Don't translate these and qt below
  if (mode == HMM_BITCOIN_QT) debugCategories += ", qt";
  strUsage +=
      HelpMessageOpt("-debug=<category>",
                     strprintf(_("Output debugging information (default: %u, supplying <category> is optional)"), 0) +
                         ". " + _("If <category> is not supplied, output all debugging information.") +
                         _("<category> can be:") + " " + debugCategories + ".");
  if (GetBoolArg("-help-debug", false))
    strUsage += HelpMessageOpt("-nodebug", "Turn off debugging messages, same as -debug=0");
  if (!fDisableWallet) {
    strUsage += HelpMessageOpt("-gen", strprintf(_("Generate coins (default: %u)"), 0));
    strUsage += HelpMessageOpt(
        "-genproclimit=<n>",
        strprintf(_("Set the number of threads for coin generation if enabled (-1 = all cores, default: %d)"), 1));
  }

  strUsage += HelpMessageOpt("-help-debug", _("Show all debugging options (usage: --help -help-debug)"));
  strUsage += HelpMessageOpt("-logips", strprintf(_("Include IP addresses in debug output (default: %u)"), 0));
  strUsage += HelpMessageOpt("-logtimestamps", strprintf(_("Prepend debug output with timestamp (default: %u)"), 1));
  if (GetBoolArg("-help-debug", false)) {
    strUsage += HelpMessageOpt(
        "-limitfreerelay=<n>",
        strprintf(_("Continuously rate-limit free transactions to <n>*1000 bytes per minute (default:%u)"), 15));
    strUsage +=
        HelpMessageOpt("-relaypriority",
                       strprintf(_("Require high priority for relaying free or low-fee transactions (default:%u)"), 1));
    strUsage += HelpMessageOpt("-maxsigcachesize=<n>",
                               strprintf(_("Limit size of signature cache to <n> entries (default: %u)"), 50000));
  }
  strUsage += HelpMessageOpt(
      "-printtoconsole", strprintf(_("Send trace/debug info to console instead of debug.log file (default: %u)"), 0));
  if (GetBoolArg("-help-debug", false)) {
    strUsage += HelpMessageOpt(
        "-printpriority", strprintf(_("Log transaction priority and fee per kB when mining blocks (default: %u)"), 0));
    strUsage += HelpMessageOpt("-privdb",
                               strprintf(_("Sets the DB_PRIVATE flag in the wallet db environment (default: %u)"), 1));
    strUsage += HelpMessageOpt(
        "-regtest",
        _("Enter regression test mode, which uses a special chain in which blocks can be solved instantly.") + " " +
            _("This is intended for regression testing tools and app development.") + " " +
            _("In this mode -genproclimit controls how many blocks are generated immediately."));
  }
  strUsage +=
      HelpMessageOpt("-shrinkdebugfile", _("Shrink debug.log file on client startup (default: 1 when no -debug)"));
  strUsage += HelpMessageOpt("-testnet", _("Use the test network"));
  strUsage += HelpMessageOpt("-litemode=<n>",
                             strprintf(_("Disable all Tessa specific functionality (Zerocoin) (0-1, default: %u)"), 0));

  if (!fDisableWallet) {
    strUsage += HelpMessageGroup(_("Staking options:"));
    strUsage += HelpMessageOpt("-staking=<n>", strprintf(_("Enable staking functionality (0-1, default: %u)"), 1));
    strUsage += HelpMessageOpt(
        "-stake=<n>", strprintf(_("Enable or disable staking functionality for Tessa inputs (0-1, default: %u)"), 1));
    strUsage += HelpMessageOpt("-reservebalance=<amt>",
                               _("Keep the specified amount available for spending at all times (default: 0)"));
    if (GetBoolArg("-help-debug", false)) {
      strUsage +=
          HelpMessageOpt("-printstakemodifier", _("Display the stake modifier calculations in the debug.log file."));
      strUsage += HelpMessageOpt("-printcoinstake", _("Display verbose coin stake messages in the debug.log file."));
    }
  }

  strUsage += HelpMessageGroup(_("Zerocoin options:"));
  strUsage += HelpMessageOpt("-reindexzerocoin=<n>",
                             strprintf(_("Delete all zerocoin spends and mints that have been recorded to the "
                                         "blockchain database and reindex them (0-1, default: %u)"),
                                       0));

  strUsage += HelpMessageGroup(_("Node relay options:"));
  strUsage += HelpMessageOpt("-datacarrier", strprintf(_("Relay and mine data carrier transactions (default: %u)"), 1));
  strUsage +=
      HelpMessageOpt("-datacarriersize",
                     strprintf(_("Maximum size of data in data carrier transactions we relay and mine (default: %u)"),
                               MAX_OP_RETURN_RELAY));
  if (GetBoolArg("-help-debug", false)) {
    strUsage += HelpMessageOpt("-blockversion=<n>", "Override block version to test forking scenarios");
  }

  strUsage += HelpMessageGroup(_("Block creation options:"));
  strUsage += HelpMessageOpt("-blockminsize=<n>", strprintf(_("Set minimum block size in bytes (default: %u)"), 0));
  strUsage += HelpMessageOpt("-blockmaxsize=<n>",
                             strprintf(_("Set maximum block size in bytes (default: %d)"), DEFAULT_BLOCK_MAX_SIZE));
  strUsage +=
      HelpMessageOpt("-blockprioritysize=<n>",
                     strprintf(_("Set maximum size of high-priority/low-fee transactions in bytes (default: %d)"),
                               DEFAULT_BLOCK_PRIORITY_SIZE));

  strUsage += HelpMessageGroup(_("RPC server options:"));
  strUsage += HelpMessageOpt("-server", _("Accept command line and JSON-RPC commands"));
  strUsage += HelpMessageOpt("-rest", strprintf(_("Accept public REST requests (default: %u)"), 0));
  strUsage += HelpMessageOpt("-rpcbind=<addr>",
                             _("Bind to given address to listen for JSON-RPC connections. Use [host]:port notation for "
                               "IPv6. This option can be specified multiple times (default: bind to all interfaces)"));
  strUsage += HelpMessageOpt("-rpccookiefile=<loc>", _("Location of the auth cookie (default: data dir)"));
  strUsage += HelpMessageOpt("-rpcuser=<user>", _("Username for JSON-RPC connections"));
  strUsage += HelpMessageOpt("-rpcpassword=<pw>", _("Password for JSON-RPC connections"));
  strUsage += HelpMessageOpt(
      "-rpcport=<port>",
      strprintf(_("Listen for JSON-RPC connections on <port> (default: %u or testnet: %u)"), 44443, 44445));
  strUsage += HelpMessageOpt("-rpcallowip=<ip>",
                             _("Allow JSON-RPC connections from specified source. Valid for <ip> are a single IP (e.g. "
                               "1.2.3.4), a network/netmask (e.g. 1.2.3.4/255.255.255.0) or a network/CIDR (e.g. "
                               "1.2.3.4/24). This option can be specified multiple times"));
  strUsage += HelpMessageOpt(
      "-rpcthreads=<n>",
      strprintf(_("Set the number of threads to service RPC calls (default: %d)"), DEFAULT_HTTP_THREADS));
  if (GetBoolArg("-help-debug", false)) {
    strUsage += HelpMessageOpt(
        "-rpcworkqueue=<n>",
        strprintf("Set the depth of the work queue to service RPC calls (default: %d)", DEFAULT_HTTP_WORKQUEUE));
    strUsage += HelpMessageOpt("-rpcservertimeout=<n>",
                               strprintf("Timeout during HTTP requests (default: %d)", DEFAULT_HTTP_SERVER_TIMEOUT));
  }
  return strUsage;
}

std::string LicenseInfo() {
  return FormatParagraph(strprintf(_("Copyright (C) 2017-%i The Tessa Core Developers"), COPYRIGHT_YEAR)) + "\n" +
         "\n" + FormatParagraph(_("This is **EXPERIMENTAL** software.")) + "\n" + "\n" +
         FormatParagraph(_(
             "Distributed under the MIT, Apache software, Boost Software and LGPL licenses, see the accompanying files "
             "Copying, LGPL Version 2.1, and APACHE_LICENSE")) +
         "\n" + "\n" + "\n";
}

static void BlockNotifyCallback(const uint256& hashNewTip) {
  std::string strCmd = GetArg("-blocknotify", "");

  strCmd.replace(strCmd.find("%s"), 2, hashNewTip.GetHex());
  std::thread(runCommand, strCmd).detach();  // thread runs free
}

static void BlockSizeNotifyCallback(int size, const uint256& hashNewTip) {
  std::string strCmd = GetArg("-blocksizenotify", "");

  strCmd.replace(strCmd.find("%s"), 2, hashNewTip.GetHex());
  strCmd.replace(strCmd.find("%d"), 2, std::to_string(size));
  std::thread(runCommand, strCmd).detach();  // thread runs free
}

struct CImportingNow {
  CImportingNow() {
    assert(fImporting == false);
    fImporting = true;
  }

  ~CImportingNow() {
    assert(fImporting == true);
    fImporting = false;
  }
};

void ThreadImport(std::vector<fs::path> vImportFiles) {
  RenameThread("tessa-loadblk");

  // -reindex
  if (fReindex) {
    CImportingNow imp;
    int nFile = 0;
    while (true) {
      CDiskBlockPos pos(nFile, 0);
      if (!fs::exists(GetBlockPosFilename(pos, "blk"))) break;  // No block files left to reindex
      FILE* file = OpenBlockFile(pos, true);
      if (!file) break;  // This error is logged in OpenBlockFile
      LogPrintf("Reindexing block file blk%05u.dat...\n", (uint32_t)nFile);
      LoadExternalBlockFile(file, &pos);
      nFile++;
    }
    gpBlockTreeDB->WriteReindexing(false);
    fReindex = false;
    LogPrintf("Reindexing finished\n");
    // To avoid ending up in a situation without genesis block, re-try initializing (no-op if reindexing worked):
    InitBlockIndex();
  }

  // hardcoded $DATADIR/bootstrap.dat
  path pathBootstrap = GetDataDir() / "bootstrap.dat";
  if (exists(pathBootstrap)) {
    FILE* file = fopen(pathBootstrap.string().c_str(), "rb");
    if (file) {
      CImportingNow imp;
      path pathBootstrapOld = GetDataDir() / "bootstrap.dat.old";
      LogPrintf("Importing bootstrap.dat...\n");
      LoadExternalBlockFile(file);
      RenameOver(pathBootstrap, pathBootstrapOld);
    } else {
      LogPrintf("Warning: Could not open bootstrap file %s\n", pathBootstrap.string());
    }
  }

  // -loadblock=
  for (fs::path& path : vImportFiles) {
    FILE* file = fopen(path.string().c_str(), "rb");
    if (file) {
      CImportingNow imp;
      LogPrintf("Importing blocks file %s...\n", path.string());
      LoadExternalBlockFile(file);
    } else {
      LogPrintf("Warning: Could not open blocks file %s\n", path.string());
    }
  }

  if (GetBoolArg("-stopafterblockimport", false)) {
    LogPrintf("Stopping after block import\n");
    StartShutdown();
  }
}

/** Sanity checks
 *  Ensure that Tessa is running in a usable environment with all
 *  necessary library support.
 */
bool InitSanityCheck() {
  if (!ECC_InitSanityCheck()) {
    InitError(
        "OpenSSL appears to lack support for elliptic curve cryptography. For more "
        "information, visit https://en.bitcoin.it/wiki/OpenSSL_and_EC_Libraries");
    return false;
  }
  // if (!glibc_sanity_test() || !glibcxx_sanity_test()) return false;

  return true;
}

bool AppInitServers() {
  RPCServer::OnStopped(&OnRPCStopped);
  RPCServer::OnPreCommand(&OnRPCPreCommand);
  if (!InitHTTPServer()) return false;
  if (!StartRPC()) return false;
  if (!StartHTTPRPC()) return false;
  if (GetBoolArg("-rest", false) && !StartREST()) return false;
  if (!StartHTTPServer()) return false;
  return true;
}

void InitLogging() {
  TessaLog::Logger& logger = GetLogger();
  logger.fPrintToConsole = gArgs.GetBoolArg("-printtoconsole", false);
  logger.fLogTimestamps = gArgs.GetBoolArg("-logtimestamps", DEFAULT_LOGTIMESTAMPS);
  logger.fLogTimeMicros = gArgs.GetBoolArg("-logtimemicros", DEFAULT_LOGTIMEMICROS);

  fLogIPs = gArgs.GetBoolArg("-logips", DEFAULT_LOGIPS);

  LogPrintf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
  LogPrintf("%s version %s\n", CLIENT_NAME, FormatFullVersion());
}

/** Initialize tessa.
 *  @pre Parameters should be parsed and config file should be read.
 */
bool AppInit2(CScheduler& scheduler) {
// ********************************************************* Step 1: setup
#ifdef _MSC_VER
  // Turn off Microsoft heap dump noise
  _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE);
  _CrtSetReportFile(_CRT_WARN, CreateFileA("NUL", GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, 0));
  // Disable confusing "helpful" text message on abort, Ctrl-C
  _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
#endif
#ifdef WIN32
// Enable Data Execution Prevention (DEP)
// Minimum supported OS versions: WinXP SP3, WinVista >= SP1, Win Server 2008
// A failure is non-critical and needs no further attention!
#ifndef PROCESS_DEP_ENABLE
// We define this here, because GCCs winbase.h limits this to _WIN32_WINNT >= 0x0601 (Windows 7),
// which is not correct. Can be removed, when GCCs winbase.h is fixed!
#define PROCESS_DEP_ENABLE 0x00000001
#endif
  typedef BOOL(WINAPI * PSETPROCDEPPOL)(DWORD);
  PSETPROCDEPPOL setProcDEPPol =
      (PSETPROCDEPPOL)GetProcAddress(GetModuleHandleA("Kernel32.dll"), "SetProcessDEPPolicy");
  if (setProcDEPPol != nullptr) setProcDEPPol(PROCESS_DEP_ENABLE);
#endif

  if (!SetupNetworking()) return InitError("Error: Initializing networking failed");

#ifndef WIN32
  if (GetBoolArg("-sysperms", false)) {
    if (!GetBoolArg("-disablewallet", false))
      return InitError("Error: -sysperms is not allowed in combination with enabled wallet functionality");

  } else {
    umask(077);
  }

  // Clean shutdown on SIGTERM
  struct sigaction sa;
  sa.sa_handler = HandleSIGTERM;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGTERM, &sa, nullptr);
  sigaction(SIGINT, &sa, nullptr);

  // Reopen debug.log on SIGHUP
  struct sigaction sa_hup;
  sa_hup.sa_handler = HandleSIGHUP;
  sigemptyset(&sa_hup.sa_mask);
  sa_hup.sa_flags = 0;
  sigaction(SIGHUP, &sa_hup, nullptr);

  // Ignore SIGPIPE, otherwise it will bring the daemon down if the client closes unexpectedly
  signal(SIGPIPE, SIG_IGN);
#endif

  // ********************************************************* Step 2: parameter interactions
  // Set this early so that parameter interactions go to console
  fLogIPs = GetBoolArg("-logips", false);

  if (gArgs.IsArgSet("-bind") || gArgs.IsArgSet("-whitebind")) {
    // when specifying an explicit binding address, you want to listen on it
    // even when -connect or -proxy is specified
    if (SoftSetBoolArg("-listen", true))
      LogPrintf("AppInit2 : parameter interaction: -bind or -whitebind set -> setting -listen=1\n");
  }

  if (gArgs.IsArgSet("-connect")) {
    // when only connecting to trusted nodes, do not seed via DNS, or listen by default
    if (SoftSetBoolArg("-dnsseed", false))
      LogPrintf("AppInit2 : parameter interaction: -connect set -> setting -dnsseed=0\n");
    if (SoftSetBoolArg("-listen", false))
      LogPrintf("AppInit2 : parameter interaction: -connect set -> setting -listen=0\n");
  }

  if (gArgs.IsArgSet("-proxy")) {
    // to protect privacy, do not listen by default if a default proxy server is specified
    if (SoftSetBoolArg("-listen", false))
      LogPrintf("%s: parameter interaction: -proxy set -> setting -listen=0\n", __func__);
    // to protect privacy, do not use UPNP when a proxy is set. The user may still specify -listen=1
    // to listen locally, so don't rely on this happening through -listen below.
    if (SoftSetBoolArg("-upnp", false))
      LogPrintf("%s: parameter interaction: -proxy set -> setting -upnp=0\n", __func__);
    // to protect privacy, do not discover addresses by default
    if (SoftSetBoolArg("-discover", false))
      LogPrintf("AppInit2 : parameter interaction: -proxy set -> setting -discover=0\n");
  }

  if (!GetBoolArg("-listen", true)) {
    // do not map ports or try to retrieve public IP when not listening (pointless)
    if (SoftSetBoolArg("-upnp", false)) LogPrintf("AppInit2 : parameter interaction: -listen=0 -> setting -upnp=0\n");
    if (SoftSetBoolArg("-discover", false))
      LogPrintf("AppInit2 : parameter interaction: -listen=0 -> setting -discover=0\n");
    if (SoftSetBoolArg("-listenonion", false))
      LogPrintf("AppInit2 : parameter interaction: -listen=0 -> setting -listenonion=0\n");
  }

  if (gArgs.IsArgSet("-externalip")) {
    // if an explicit public IP is specified, do not try to find others
    if (SoftSetBoolArg("-discover", false))
      LogPrintf("AppInit2 : parameter interaction: -externalip set -> setting -discover=0\n");
  }

  // -zapwallettx implies a rescan
  if (GetBoolArg("-zapwallettxes", false)) {
    if (SoftSetBoolArg("-rescan", true))
      LogPrintf("AppInit2 : parameter interaction: -zapwallettxes=<mode> -> setting -rescan=1\n");
  }

  if (gArgs.IsArgSet("-reservebalance")) {
    int64_t bal;
    if (!ParseMoney(gArgs.GetArg("-reservebalance", ""), bal)) {
      InitError(_("Invalid amount for -reservebalance=<amount>"));
      return false;
    }
    setReserveBalance(bal);
  }

  // Make sure enough file descriptors are available
  int nBind = std::max((int)gArgs.IsArgSet("-bind") + (int)gArgs.IsArgSet("-whitebind"), 1);
  nMaxConnections = GetArg("-maxconnections", 125);
  nMaxConnections = std::max(std::min(nMaxConnections, (int)(FD_SETSIZE - nBind - MIN_CORE_FILEDESCRIPTORS)), 0);
  int nFD = RaiseFileDescriptorLimit(nMaxConnections + MIN_CORE_FILEDESCRIPTORS);
  if (nFD < MIN_CORE_FILEDESCRIPTORS) return InitError(_("Not enough file descriptors available."));
  if (nFD - MIN_CORE_FILEDESCRIPTORS < nMaxConnections) nMaxConnections = nFD - MIN_CORE_FILEDESCRIPTORS;

  // ********************************************************* Step 3: parameter-to-internal-flags
  if (gArgs.IsArgSet("-debug")) {
    // Special-case: if -debug=0/-nodebug is set, turn off debugging
    // messages
    const std::vector<std::string>& categories = gArgs.GetArgs("-debug");
    if (find(categories.begin(), categories.end(), std::string("0")) == categories.end()) {
      for (const auto& cat : categories) {
        TessaLog::LogFlags flag;
        if (!GetLogCategory(flag, cat)) {
          InitWarning(strprintf(_("Unsupported logging category %s=%s."), "-debug", cat));
        }
        GetLogger().EnableCategory(flag);
      }
    }
  }

  // Check level must be 4 for zerocoin checks
  if (gArgs.IsArgSet("-checklevel"))
    return InitError(_("Error: Unsupported argument -checklevel found. Checklevel must be level 4."));

  // Checkmempool and checkblockindex default to true in regtest mode
  mempool.setSanityCheck(GetBoolArg("-checkmempool", Params().DefaultConsistencyChecks()));
  fCheckBlockIndex = GetBoolArg("-checkblockindex", Params().DefaultConsistencyChecks());
  Checkpoints::fEnabled = GetBoolArg("-checkpoints", true);

  // -par=0 means autodetect, but nScriptCheckThreads==0 means no concurrency
  nScriptCheckThreads = GetArg("-par", DEFAULT_SCRIPTCHECK_THREADS);
  if (nScriptCheckThreads <= 0) nScriptCheckThreads += std::thread::hardware_concurrency();
  if (nScriptCheckThreads <= 1)
    nScriptCheckThreads = 0;
  else if (nScriptCheckThreads > MAX_SCRIPTCHECK_THREADS)
    nScriptCheckThreads = MAX_SCRIPTCHECK_THREADS;

  bool fServer = gArgs.GetBoolArg("-server", false);
  setvbuf(stdout, nullptr, _IOLBF, 0);  /// ***TODO*** do we still need this after -printtoconsole is gone?

  // Staking needs a CWallet instance, so make sure wallet is enabled
  fDisableWallet = GetBoolArg("-disablewallet", false);
  if (fDisableWallet) {
    if (SoftSetBoolArg("-staking", false))
      LogPrintf("AppInit2 : parameter interaction: wallet functionality not enabled -> setting -staking=0\n");
  }

  nConnectTimeout = GetArg("-timeout", DEFAULT_CONNECT_TIMEOUT);
  if (nConnectTimeout <= 0) nConnectTimeout = DEFAULT_CONNECT_TIMEOUT;

  if (!fDisableWallet) {
    nTxConfirmTarget = GetArg("-txconfirmtarget", 1);
    bSpendZeroConfChange = GetBoolArg("-spendzeroconfchange", false);
    bdisableSystemnotifications = GetBoolArg("-disablesystemnotifications", false);
    fSendFreeTransactions = GetBoolArg("-sendfreetransactions", false);
  }
  std::string strWalletDir = GetArg("-wallet", "wallet");
  fs::path strWalletPath = GetDataDir();
  strWalletPath = strWalletPath / strWalletDir;

  fIsBareMultisigStd = GetBoolArg("-permitbaremultisig", true) != 0;
  nMaxDatacarrierBytes = GetArg("-datacarriersize", nMaxDatacarrierBytes);

  if (GetBoolArg("-peerbloomfilters", DEFAULT_PEERBLOOMFILTERS)) nLocalServices |= NODE_BLOOM;

  // ********************************************************* Step 4: application initialization: dir lock, daemonize,
  // pidfile, debug log

  // Initialize elliptic curve code
  if (sodium_init() < 0) { throw string("Libsodium initialization failed."); }
  ECC_Start();
  globalVerifyHandle.reset(new ECCVerifyHandle());

  // Sanity check
  if (!InitSanityCheck()) return InitError(_("Initialization sanity check failed. Tessa Core is shutting down."));

  std::string strDataDir = GetDataDir().string();
  if (!fDisableWallet) {
    // Wallet file must be a plain filename without a directory
#ifndef NO_BOOST_FILESYSTEM
    if (strWalletDir != fs::basename(strWalletDir) + fs::extension(strWalletDir))
      return InitError(strprintf(_("Wallet %s resides outside data directory %s"), strWalletDir, strDataDir));
#endif
  }
  // Make sure only a single Tessa process is using the data directory.
  fs::path pathLockFile = GetDataDir() / ".lock";
  FILE* file = fopen(pathLockFile.string().c_str(), "a");  // empty lock file; created if it doesn't exist.
  if (file) fclose(file);

#ifndef NO_BOOST_FILESYSTEM
  static boost::interprocess::file_lock lock(pathLockFile.string().c_str());

  // Wait maximum 10 seconds if an old wallet is still running. Avoids lockup during restart
  if (!lock.timed_lock(boost::get_system_time() + boost::posix_time::seconds(10)))
    return InitError(
        strprintf(_("Cannot obtain a lock on data directory %s. Tessa Core is probably already running."), strDataDir));
#else
  LogPrintf("Skipping checking of .lock file\n");
#endif

#ifndef WIN32
  CreatePidFile(GetPidFile(), getpid());
#endif

  TessaLog::Logger& logger = GetLogger();

  bool default_shrinkdebugfile = logger.DefaultShrinkDebugFile();
  if (gArgs.GetBoolArg("-shrinkdebugfile", default_shrinkdebugfile)) {
    // Do this first since it both loads a bunch of debug.log into memory,
    // and because this needs to happen before any other debug.log printing.
    logger.ShrinkDebugFile();
  }

  if (logger.fPrintToDebugLog) { logger.OpenDebugLog(); }

  if (!logger.fLogTimestamps) { LogPrintf("Startup time: %s\n", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime())); }

  LogPrintf("\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n\n");
  LogPrintf("Tessa version %s (%s)\n", FormatFullVersion(), CLIENT_DATE);
  // LogPrintf("Using OpenSSL version %s\n", SSLeay_version(SSLEAY_VERSION));
  // LogPrintf("Using BerkeleyDB version %s\n", DbEnv::version(0, 0, 0));
  if (!logger.fLogTimestamps) LogPrintf("Startup time: %s\n", DateTimeStrFormat("%Y-%m-%d %H:%M:%S", GetTime()));
  LogPrintf("Default data directory %s\n", GetDefaultDataDir().string());
  LogPrintf("Using data directory %s\n", strDataDir);
  LogPrintf("Using config file %s\n", GetConfigFile().string());
  LogPrintf("Using at most %i connections (%i file descriptors available)\n", nMaxConnections, nFD);
  std::ostringstream strErrors;

  LogPrintf("Using %u threads for script verification\n", nScriptCheckThreads);
  if (nScriptCheckThreads) {
    script_check_threads.reserve(nScriptCheckThreads - 1);
    for (int i = 0; i < nScriptCheckThreads - 1; i++) script_check_threads.emplace_back(&ThreadScriptCheck);
  }

  if (gArgs.IsArgSet("-sporkkey"))  // spork priv key
  {
    if (!gSporkManager.SetPrivKey(GetArg("-sporkkey", "")))
      return InitError(_("Unable to sign spork message, wrong key?"));
  }

  // Start the lightweight task scheduler thread
  CScheduler::Function serviceLoop = [&] { scheduler.serviceQueue(); };
  scheduler_thread = std::thread(std::bind(&TraceThread<CScheduler::Function>, "scheduler", serviceLoop));

  /* Start the RPC server already.  It will be started in "warmup" mode
   * and not really process calls already (but it will signify connections
   * that the server is there and will be ready later).  Warmup mode will
   * be disabled when initialisation is finished.
   */
  if (fServer) {
    uiInterface.InitMessage.connect(SetRPCWarmupStatus);
    if (!AppInitServers()) return InitError(_("Unable to start HTTP server. See debug log for details."));
  }

  int64_t nStart;

  // ********************************************************* Step 5: Backup wallet and verify wallet database
  // integrity
  if (!fDisableWallet) {
    path backupDir = GetDataDir() / "backups";
    if (!exists(backupDir)) {
      // Always create backup folder to not confuse the operating system's file browser
      create_directories(backupDir);
    }
#ifndef NO_BOOST_FILESYSTEM
    if (nWalletBackups > 0) {
      if (exists(backupDir)) {
        // Create backup of the wallet
        std::string dateTimeStr = DateTimeStrFormat(".%Y-%m-%d-%H-%M", GetTime());
        std::string backupPathStr = backupDir.string();
        backupPathStr += "/" + strWalletFile;
        std::string sourcePathStr = GetDataDir().string();
        sourcePathStr += "/" + strWalletDir + "/" + strWalletFile;
        fs::path sourceFile = sourcePathStr;
        fs::path backupFile = backupPathStr + dateTimeStr;
        sourceFile.make_preferred();
        backupFile.make_preferred();
        if (fs::exists(sourceFile)) {
          try {
            fs::copy_file(sourceFile, backupFile);
            LogPrintf("Creating backup of %s -> %s\n", sourceFile, backupFile);
          } catch (fs::filesystem_error& error) { LogPrintf("Failed to create backup %s\n", error.what()); }
        }
        // Keep only the last several backups (nWalletBackup), including the new one of course
        using folder_set_t = std::multimap<std::time_t, fs::path>;
        folder_set_t folder_set;
        fs::directory_iterator end_iter;
        fs::path backupFolder = backupDir.string();
        backupFolder.make_preferred();
        // Build map of backup files for current(!) wallet sorted by last write time
        fs::path currentFile;
        for (fs::directory_iterator dir_iter(backupFolder); dir_iter != end_iter; ++dir_iter) {
          // Only check regular files
          if (fs::is_regular_file(dir_iter->status())) {
            currentFile = dir_iter->path().filename();
            // Only add the backups for the current wallet, e.g. wallet.*
            if (dir_iter->path().stem().string() == strWalletFile) {
              folder_set.insert(folder_set_t::value_type(fs::last_write_time(dir_iter->path()), *dir_iter));
            }
          }
        }
        // Loop backward through backup files and keep the N newest ones (1 <= N <= 10)
        int counter = 0;
        for (auto& file : reverse_iterate(folder_set)) {
          counter++;
          if (counter > nWalletBackups) {
            // More than nWalletBackups backups: delete oldest one(s)
            try {
              fs::remove(file.second);
              LogPrintf("Old backup deleted: %s\n", file.second);
            } catch (fs::filesystem_error& error) { LogPrintf("Failed to delete backup %s\n", error.what()); }
          }
        }
      }
    }
    //#endif

    if (GetBoolArg("-resync", false)) {
      uiInterface.InitMessage.fire(_("Preparing for resync..."));
      // Delete the local blockchain folders to force a resync from scratch to get a consitent blockchain-state
      path blocksDir = GetDataDir() / "blocks";
      path chainstateDir = GetDataDir() / "chainstate";
      path sporksDir = GetDataDir() / "sporks";
      path zerocoinDir = GetDataDir() / "zerocoin";

      LogPrintf("Deleting blockchain folders blocks, chainstate, sporks and zerocoin\n");
      // We delete in 4 individual steps in case one of the folder is missing already
      try {
        if (exists(blocksDir)) {
          fs::remove_all(blocksDir);
          LogPrintf("-resync: folder deleted: %s\n", blocksDir.string().c_str());
        }

        if (exists(chainstateDir)) {
          fs::remove_all(chainstateDir);
          LogPrintf("-resync: folder deleted: %s\n", chainstateDir.string().c_str());
        }

        if (exists(sporksDir)) {
          fs::remove_all(sporksDir);
          LogPrintf("-resync: folder deleted: %s\n", sporksDir.string().c_str());
        }

        if (exists(zerocoinDir)) {
          fs::remove_all(zerocoinDir);
          LogPrintf("-resync: folder deleted: %s\n", zerocoinDir.string().c_str());
        }
      } catch (fs::filesystem_error& error) { LogPrintf("Failed to delete blockchain folders %s\n", error.what()); }
    }
#endif

    LogPrintf("Using wallet %s\n", strWalletDir);
    uiInterface.InitMessage.fire(_("Verifying wallet..."));

    if (gWalletDB.init(strWalletPath)) {
      // try moving env out of the way
      fs::path pathDatabaseBak = GetDataDir() / strprintf("wallet.%d.bak", GetTime());
#ifndef NO_BOOST_FILESYSTEM
      try {
        RenameOver(strWalletPath, pathDatabaseBak);
        LogPrintf("Moved old %s to %s. Retrying.\n", strWalletPath.string(), pathDatabaseBak.string());
      } catch (fs::filesystem_error& error) {
        // failure is ok (well, not really, but it's not worse than what we started with)
      }
#else
      RenameOver(strWalletPath, pathDatabaseBak);
      // fs::rename(strWalletPath, pathDatabaseBak);
#endif

      // try again
      if (gWalletDB.init(strWalletPath)) {
        // if it still fails, it probably means we can't even create the database env
        string msg = strprintf(_("Error initializing wallet database environment %s!"), strDataDir);
        return InitError(msg);
      }
    }
  }  // (!fDisableWallet)
  // ********************************************************* Step 6: network initialization

  RegisterNodeSignals(GetNodeSignals());

  if (gArgs.IsArgSet("-onlynet")) {
    std::set<enum Network> nets;
    for (const auto& snet : gArgs.GetArgs("-onlynet")) {
      enum Network net = ParseNetwork(snet);
      if (net == NET_UNROUTABLE) return InitError(strprintf(_("Unknown network specified in -onlynet: '%s'"), snet));
      nets.insert(net);
    }
    for (int n = 0; n < NET_MAX; n++) {
      auto net = (enum Network)n;
      if (!nets.count(net)) SetLimited(net);
    }
  }

  if (gArgs.IsArgSet("-whitelist")) {
    for (const std::string& net : gArgs.GetArgs("-whitelist")) {
      CSubNet subnet(net);
      if (!subnet.IsValid()) return InitError(strprintf(_("Invalid netmask specified in -whitelist: '%s'"), net));
      CNode::AddWhitelistedRange(subnet);
    }
  }

  // Check for host lookup allowed before parsing any network related parameters
  fNameLookup = GetBoolArg("-dns", DEFAULT_NAME_LOOKUP);

  bool proxyRandomize = GetBoolArg("-proxyrandomize", true);
  // -proxy sets a proxy for all outgoing network traffic
  // -noproxy (or -proxy=0) as well as the empty string can be used to not set a proxy, this is the default
  std::string proxyArg = GetArg("-proxy", "");
  SetLimited(NET_TOR);
  if (proxyArg != "" && proxyArg != "0") {
    CService proxyAddr;
    if (!Lookup(proxyArg.c_str(), proxyAddr, 9050, fNameLookup)) {
      return InitError(strprintf(_("Lookup(): Invalid -proxy address or hostname: '%s'"), proxyArg));
    }

    proxyType addrProxy = proxyType(proxyAddr, proxyRandomize);
    if (!addrProxy.IsValid())
      return InitError(strprintf(_("isValid(): Invalid -proxy address or hostname: '%s'"), proxyArg));

    SetProxy(NET_IPV4, addrProxy);
    SetProxy(NET_IPV6, addrProxy);
    SetProxy(NET_TOR, addrProxy);
    SetNameProxy(addrProxy);
    SetLimited(NET_TOR, false);  // by default, -proxy sets onion as reachable, unless -noonion later
  }

  // -onion can be used to set only a proxy for .onion, or override normal proxy for .onion addresses
  // -noonion (or -onion=0) disables connecting to .onion entirely
  // An empty string is used to not override the onion proxy (in which case it defaults to -proxy set above, or none)
  std::string onionArg = GetArg("-onion", "");
  if (onionArg != "") {
    if (onionArg == "0") {  // Handle -noonion/-onion=0
      SetLimited(NET_TOR);  // set onions as unreachable
    } else {
      CService onionProxy;
      if (!Lookup(onionArg.c_str(), onionProxy, 9050, fNameLookup)) {
        return InitError(strprintf(_("Invalid -onion address or hostname: '%s'"), onionArg));
      }
      proxyType addrOnion = proxyType(onionProxy, proxyRandomize);
      if (!addrOnion.IsValid()) return InitError(strprintf(_("Invalid -onion address or hostname: '%s'"), onionArg));
      SetProxy(NET_TOR, addrOnion);
      SetLimited(NET_TOR, false);
    }
  }

  // see Step 2: parameter interactions for more information about these
  fListen = GetBoolArg("-listen", DEFAULT_LISTEN);
  fDiscover = GetBoolArg("-discover", true);

  bool fBound = false;
  if (fListen) {
    if (gArgs.IsArgSet("-bind") || gArgs.IsArgSet("-whitebind")) {
      for (const std::string& strBind : gArgs.GetArgs("-bind")) {
        CService addrBind;
        if (!Lookup(strBind.c_str(), addrBind, GetListenPort(), false))
          return InitError(strprintf(_("Cannot resolve -bind address: '%s'"), strBind));
        fBound |= Bind(addrBind, (BF_EXPLICIT | BF_REPORT_ERROR));
      }
      for (const std::string& strBind : gArgs.GetArgs("-whitebind")) {
        CService addrBind;
        if (!Lookup(strBind.c_str(), addrBind, 0, false))
          return InitError(strprintf(_("Cannot resolve -whitebind address: '%s'"), strBind));
        if (addrBind.GetPort() == 0)
          return InitError(strprintf(_("Need to specify a port with -whitebind: '%s'"), strBind));
        fBound |= Bind(addrBind, (BF_EXPLICIT | BF_REPORT_ERROR | BF_WHITELIST));
      }
    } else {
      struct in_addr inaddr_any;
      inaddr_any.s_addr = INADDR_ANY;
      fBound |= Bind(CService(in6addr_any, GetListenPort()), BF_NONE);
      fBound |= Bind(CService(inaddr_any, GetListenPort()), !fBound ? BF_REPORT_ERROR : BF_NONE);
    }
    if (!fBound) return InitError(_("Failed to listen on any port. Use -listen=0 if you want this."));
  }

  if (gArgs.IsArgSet("-externalip")) {
    for (const std::string& strAddr : gArgs.GetArgs("-externalip")) {
      CService addrLocal(strAddr, GetListenPort(), fNameLookup);
      if (!addrLocal.IsValid()) return InitError(strprintf(_("Cannot resolve -externalip address: '%s'"), strAddr));
      AddLocal(CService(strAddr, GetListenPort(), fNameLookup), LOCAL_MANUAL);
    }
  }

  for (const string& strDest : gArgs.GetArgs("-seednode")) AddOneShot(strDest);

#if ENABLE_ZMQ
  pzmqNotificationInterface = CZMQNotificationInterface::Create();

  if (pzmqNotificationInterface) { RegisterValidationInterface(pzmqNotificationInterface); }
#endif

  // ********************************************************* Step 7: load block chain

  // Tessa: Load Accumulator Checkpoints according to network (main/test/regtest)
  AccumulatorCheckpoints::LoadCheckpoints(Params().NetworkIDString());

  fReindex = GetBoolArg("-reindex", false);

  path blocksDir = GetDataDir() / "blocks";
  if (!exists(blocksDir)) { create_directories(blocksDir); }

  // cache size calculations
  size_t nTotalCache = (GetArg("-dbcache", nDefaultDbCache) << 20);
  if (nTotalCache < (nMinDbCache << 20))
    nTotalCache = (nMinDbCache << 20);  // total cache cannot be less than nMinDbCache
  else if (nTotalCache > (nMaxDbCache << 20))
    nTotalCache = (nMaxDbCache << 20);  // total cache cannot be greater than nMaxDbCache
  size_t nBlockTreeDBCache = nTotalCache / 8;
  if (nBlockTreeDBCache > (1 << 21) && !GetBoolArg("-txindex", true))
    nBlockTreeDBCache = (1 << 21);  // block tree db cache shouldn't be larger than 2 MiB
  nTotalCache -= nBlockTreeDBCache;
  size_t nCoinDBCache = nTotalCache / 2;  // use half of the remaining cache for coindb cache
  nTotalCache -= nCoinDBCache;
  nCoinCacheSize = nTotalCache / 300;  // coins in memory require around 300 bytes

  bool fLoaded = false;
  while (!fLoaded) {
    bool fReset = fReindex;
    std::string strLoadError;

    uiInterface.InitMessage.fire(_("Loading block index..."));

    nStart = GetTimeMillis();
    do {
      UnloadBlockIndex();
      gSporkDB.init((GetDataDir() / "sporks.json").string());

      try {
        // Tessa specific: zerocoin and spork DB's
        gpZerocoinDB.reset(new CZerocoinDB(0, false, fReindex));
      } catch (std::exception& e) {
        if (gArgs.IsArgSet("-debug")) LogPrintf("%s\n", e.what());
        strLoadError = _("Error opening Zerocoin DB");
        fVerifyingBlocks = false;
        break;
      }

      try {
        gpBlockTreeDB.reset(new CBlockTreeDB(nBlockTreeDBCache, false, fReindex));
      } catch (std::exception& e) {
        if (gArgs.IsArgSet("-debug")) LogPrintf("%s\n", e.what());
        strLoadError = _("Error opening block DB");
        fVerifyingBlocks = false;
        break;
      }

      try {
        delete pcoinsdbview;
        pcoinsdbview = new CCoinsViewDB(nCoinDBCache, false, fReindex);
      } catch (std::exception& e) {
        if (gArgs.IsArgSet("-debug")) LogPrintf("%s\n", e.what());
        strLoadError = _("Error opening CoinsView DB");
        fVerifyingBlocks = false;
        break;
      }

      try {
        delete pcoinscatcher;
        pcoinscatcher = new CCoinsViewErrorCatcher(pcoinsdbview);
      } catch (std::exception& e) {
        if (gArgs.IsArgSet("-debug")) LogPrintf("%s\n", e.what());
        strLoadError = _("Error opening coinscatcher");
        fVerifyingBlocks = false;
        break;
      }

      try {
        delete gpCoinsTip;
        gpCoinsTip = new CCoinsViewCache(pcoinscatcher);
      } catch (std::exception& e) {
        if (gArgs.IsArgSet("-debug")) LogPrintf("%s\n", e.what());
        strLoadError = _("Error opening CoinsTip/CoinvViewCache");
        fVerifyingBlocks = false;
        break;
      }

      try {
        if (fReindex) gpBlockTreeDB->WriteReindexing(true);

        // Tessa: load previous sessions sporks if we have them.
        uiInterface.InitMessage.fire(_("Loading sporks..."));
        gSporkManager.LoadSporksFromDB();

        uiInterface.InitMessage.fire(_("Loading block index..."));
        string strBlockIndexError = "";
        if (!LoadBlockIndex(strBlockIndexError)) {
          strLoadError = _("Error loading block database");
          strLoadError = strprintf("%s : %s", strLoadError, strBlockIndexError);
          break;
        }

        // If the loaded chain has a wrong genesis, bail out immediately
        // (we're likely using a testnet datadir, or the other way around).
        if (!mapBlockIndex.empty() && mapBlockIndex.count(Params().HashGenesisBlock()) == 0)
          return InitError(_("Incorrect or no genesis block found. Wrong datadir for network?"));

        // Initialize the block index (no-op if non-empty database was already loaded)
        if (!InitBlockIndex()) {
          strLoadError = _("Error initializing block database");
          break;
        }

        // Check for changed -txindex state
        if (fTxIndex != GetBoolArg("-txindex", true)) {
          strLoadError = _("You need to rebuild the database using -reindex to change -txindex");
          break;
        }

        // Drop all information from the zerocoinDB and repopulate
        if (GetBoolArg("-reindexzerocoin", false)) {
          uiInterface.InitMessage.fire(_("Reindexing zerocoin database..."));
          std::string strError = ReindexZerocoinDB();
          if (strError != "") {
            strLoadError = strError;
            break;
          }
        }

        // Force recalculation of accumulators.
        if (GetBoolArg("-reindexaccumulators", false)) {
          CBlockIndex* pindex = chainActive[Params().Zerocoin_StartHeight()];
          while (pindex->nHeight < chainActive.Height()) {
            if (!count(listAccCheckpointsNoDB.begin(), listAccCheckpointsNoDB.end(), pindex->nAccumulatorCheckpoint))
              listAccCheckpointsNoDB.emplace_back(pindex->nAccumulatorCheckpoint);
            pindex = chainActive.Next(pindex);
          }
        }

        // Tessa: recalculate Accumulator Checkpoints that failed to database properly
        if (!listAccCheckpointsNoDB.empty()) {
          uiInterface.InitMessage.fire(_("Calculating missing accumulators..."));
          LogPrintf("%s : finding missing checkpoints\n", __func__);

          string strError;
          if (!ReindexAccumulators(listAccCheckpointsNoDB, strError)) return InitError(strError);
        }

        uiInterface.InitMessage.fire(_("Verifying blocks..."));

        // Flag sent to validation code to let it know it can skip certain checks
        fVerifyingBlocks = true;

        // Zerocoin must check at level 4
        if (!CVerifyDB().VerifyDB(pcoinsdbview, 4, GetArg("-checkblocks", 100))) {
          strLoadError = _("Corrupted block database detected during VerifyDB");
          fVerifyingBlocks = false;
          break;
        }
      } catch (std::exception& e) {
        if (gArgs.IsArgSet("-debug")) LogPrintf("%s\n", e.what());
        strLoadError = _("Other Error/Exception on Initialization");
        fVerifyingBlocks = false;
        break;
      }

      fVerifyingBlocks = false;
      fLoaded = true;
    } while (false);

    // When Fails...
    if (!fLoaded) {
      // first suggest a reindex
      if (!fReset) {
        bool fRet;
        uiInterface.ThreadSafeMessageBox.fire(
            strLoadError + ".\n\n" + _("Do you want to rebuild the block database now?"), "",
            CClientUIInterface::MSG_ERROR | CClientUIInterface::BTN_ABORT, &fRet);
        if (fRet) {
          fReindex = true;
          fRequestShutdown = false;
        } else {
          LogPrintf("Aborted block database rebuild. Exiting.\n");
          return false;
        }
      } else {
        return InitError(strLoadError);
      }
    }
  }

  // As LoadBlockIndex can take several minutes, it's possible the user
  // requested to kill the GUI during the last operation. If so, exit.
  // As the program has not fully started yet, Shutdown() is possibly overkill.
  if (fRequestShutdown) {
    LogPrintf("Shutdown requested. Exiting.\n");
    return false;
  }
  LogPrintf(" block index %15dms\n", GetTimeMillis() - nStart);

  // ********************************************************* Step 8: load wallet

  if (fDisableWallet) {
    pwalletMain = nullptr;
    zwalletMain = nullptr;
    LogPrintf("Wallet disabled!\n");
  } else {
    // needed to restore wallet transaction meta data after -zapwallettxes
    std::vector<CWalletTx> vWtx;

    if (GetBoolArg("-zapwallettxes", false)) {
      uiInterface.InitMessage.fire(_("Zapping all transactions from wallet..."));

      pwalletMain = new CWallet;
      DBErrors nZapWalletRet = pwalletMain->ZapWalletTx(vWtx);
      if (nZapWalletRet != DB_LOAD_OK) {
        uiInterface.InitMessage.fire(_("Error loading wallet.dat: Wallet corrupted"));
        return false;
      }

      delete pwalletMain;
      pwalletMain = nullptr;
    }

    uiInterface.InitMessage.fire(_("Loading wallet..."));
    fVerifyingBlocks = true;

    nStart = GetTimeMillis();
    bool fFirstRun = true;
    pwalletMain = new CWallet;
    DBErrors nLoadWalletRet = pwalletMain->LoadWallet(fFirstRun);
    if (nLoadWalletRet != DB_LOAD_OK) {
      if (nLoadWalletRet == DB_CORRUPT)
        strErrors << _("Error loading wallet.dat: Wallet corrupted") << "\n";
      else if (nLoadWalletRet == DB_NONCRITICAL_ERROR) {
        string msg(
            _("Warning: error reading wallet.dat! All keys read correctly, but transaction data"
              " or address book entries might be missing or incorrect."));
        InitWarning(msg);
      } else if (nLoadWalletRet == DB_TOO_NEW)
        strErrors << _("Error loading wallet.dat: Wallet requires newer version of Tessa Core") << "\n";
      else if (nLoadWalletRet == DB_NEED_REWRITE) {
        strErrors << _("Wallet needed to be rewritten: restart Tessa Core to complete") << "\n";
        LogPrintf("%s", strErrors.str());
        return InitError(strErrors.str());
      } else
        strErrors << _("Error loading wallet.dat") << "\n";
    }

    zwalletMain = new CZeroWallet;
    pwalletMain->setZWallet(zwalletMain);

    if (fFirstRun) {
      // Get/Set Password
      // Check if QT or Not
      SecureString passphrase;
      passphrase.reserve(1024);
      passphrase.assign("1234");

      pwalletMain->SetupCrypter(passphrase);

      // Create new keyUser and set as default key
      // Also setups pool of 200(?) keys -> calls TopUpKeyPool
      ecdsa::CPubKey newDefaultKey;
      if (pwalletMain->GetKeyFromPool(newDefaultKey)) {
        pwalletMain->SetDefaultKey(newDefaultKey);
        if (!pwalletMain->SetAddressBook(pwalletMain->vchDefaultKey.GetID(), "", "receive"))
          strErrors << _("Cannot write default address") << "\n";
      }

      pwalletMain->SetBestChain(chainActive.GetLocator());

      LogPrintf("%s", strErrors.str());
      LogPrintf(" wallet      %15dms\n", GetTimeMillis() - nStart);

    } else {
      SecureString passphrase;
      passphrase.reserve(1024);
      passphrase.assign("1234");

      if (!pwalletMain->Unlock(passphrase)) { throw string("Couldn't unlock wallet with password"); }
    }

    RegisterValidationInterface(pwalletMain);

    CBlockIndex* pindexRescan = chainActive.Tip();
    if (GetBoolArg("-rescan", false))
      pindexRescan = chainActive.Genesis();
    else {
      CBlockLocator locator;
      if (gWalletDB.ReadBestBlock(locator))
        pindexRescan = FindForkInGlobalIndex(chainActive, locator);
      else
        pindexRescan = chainActive.Genesis();
    }
    if (chainActive.Tip() && chainActive.Tip() != pindexRescan) {
      uiInterface.InitMessage.fire(_("Rescanning..."));
      LogPrintf("Rescanning last %i blocks (from block %i)...\n", chainActive.Height() - pindexRescan->nHeight,
                pindexRescan->nHeight);
      nStart = GetTimeMillis();
      pwalletMain->ScanForWalletTransactions(pindexRescan, true);
      LogPrintf(" rescan      %15dms\n", GetTimeMillis() - nStart);
      pwalletMain->SetBestChain(chainActive.GetLocator());

      // Restore wallet transaction metadata after -zapwallettxes=1
      if (GetBoolArg("-zapwallettxes", false) && GetArg("-zapwallettxes", "1") != "2") {
        for (const CWalletTx& wtxOld : vWtx) {
          uint256 hash = wtxOld.GetHash();
          auto mi = pwalletMain->mapWallet.find(hash);
          if (mi != pwalletMain->mapWallet.end()) {
            const CWalletTx* copyFrom = &wtxOld;
            CWalletTx* copyTo = &mi->second;
            copyTo->mapValue = copyFrom->mapValue;
            copyTo->vOrderForm = copyFrom->vOrderForm;
            copyTo->nTimeReceived = copyFrom->nTimeReceived;
            copyTo->nTimeSmart = copyFrom->nTimeSmart;
            copyTo->fFromMe = copyFrom->fFromMe;
            copyTo->strFromAccount = copyFrom->strFromAccount;
            copyTo->nOrderPos = copyFrom->nOrderPos;
            copyTo->WriteToDisk();
          }
        }
      }
    }
    fVerifyingBlocks = false;

    // Inititalize ZkpWallet
    uiInterface.InitMessage.fire(_("Syncing ZKP wallet..."));

    bool fEnableZkpBackups = GetBoolArg("-backupzkp", true);
    pwalletMain->setZkpAutoBackups(fEnableZkpBackups);

    // Load zerocoin mint hashes to memory
    pwalletMain->zkpTracker->Init();
    zwalletMain->LoadMintPoolFromDB();
    zwalletMain->SyncWithChain();

    uiInterface.InitMessage.fire(_("ZKP wallet synced"));

  }  // (!fDisableWallet)
  // ********************************************************* Step 9: import blocks

  if (gArgs.IsArgSet("-blocknotify")) uiInterface.NotifyBlockTip.connect(BlockNotifyCallback);

  if (gArgs.IsArgSet("-blocksizenotify")) uiInterface.NotifyBlockSize.connect(BlockSizeNotifyCallback);

  // scan for better chains in the block chain database, that are not yet connected in the active best chain
  CValidationState state;
  if (!ActivateBestChain(state)) strErrors << "Failed to connect best block";

  std::vector<fs::path> vImportFiles;
  if (gArgs.IsArgSet("-loadblock")) {
    for (string strFile : gArgs.GetArgs("-loadblock")) vImportFiles.push_back(strFile);
  }

  auto import_bind = std::bind(ThreadImport, vImportFiles);
  import_thread = std::thread(&TraceThread<decltype(import_bind)>, "bitcoin-import", std::move(import_bind));

  if (chainActive.Tip() == nullptr) {
    LogPrintf("Waiting for genesis block to be imported...\n");
    while (!fRequestShutdown && chainActive.Tip() == nullptr) MilliSleep(10);
  }

  // ********************************************************* Step 11: start node

  if (!CheckDiskSpace()) return false;

  if (!strErrors.str().empty()) return InitError(strErrors.str());

  // RandAddSeedPerfmon();

  //// debug print
  LogPrintf("mapBlockIndex.size() = %u\n", mapBlockIndex.size());
  LogPrintf("chainActive.Height() = %d\n", chainActive.Height());
  if (!fDisableWallet) {
    LogPrintf("setKeyPool.size() = %u\n", pwalletMain ? pwalletMain->setKeyPool.size() : 0);
    LogPrintf("mapWallet.size() = %u\n", pwalletMain ? pwalletMain->mapWallet.size() : 0);
    LogPrintf("mapAddressBook.size() = %u\n", pwalletMain ? pwalletMain->mapAddressBook.size() : 0);
  }

  StartNode(scheduler);

  // Generate coins in the background
  if (pwalletMain) GenerateBitcoins(GetBoolArg("-gen", false), pwalletMain, GetArg("-genproclimit", 1));

  // ********************************************************* Step 12: finished

  SetRPCWarmupFinished();
  uiInterface.InitMessage.fire(_("Done loading"));

  if (pwalletMain) {
    // Add wallet transactions that aren't already in a block to mapTransactions
    pwalletMain->ReacceptWalletTransactions();
  }

  return !fRequestShutdown;
}
