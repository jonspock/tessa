// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "chainparams.h"
#include "clientversion.h"
#include "fs.h"
#include "fs_utils.h"
#include "httprpc.h"
#include "httpserver.h"
#include "init.h"
#include "main.h"
#include "noui.h"
#include "rpc/server.h"
#include "scheduler.h"
#include "ui_interface.h"
#include "util.h"
#include "utiltime.h"

#include <thread>

/* Introduction text for doxygen: */

/*! \mainpage Developer documentation
 *
 * \section intro_sec Introduction
 *
 * This is the developer documentation of the reference client for an experimental new digital currency called Tessa
 * (http://www.tessa.org), which enables instant payments to anyone, anywhere in the world. Tessa uses peer-to-peer
 * technology to operate with no central authority: managing transactions and issuing money are carried out collectively
 * by the network.
 *
 * The software is a community-driven open source project, released under the MIT license.
 *
 * \section Navigation
 * Use the buttons <code>Namespaces</code>, <code>Classes</code> or <code>Files</code> at the top of the page to start
 * navigating the code.
 */

static bool fDaemon;

void WaitForShutdown(CScheduler& scheduler) {
  bool fShutdown = ShutdownRequested();
  // Tell the main threads to shutdown.
  while (!fShutdown) {
    MilliSleep(200);
    fShutdown = ShutdownRequested();
  }
  Interrupt(scheduler);
}

//////////////////////////////////////////////////////////////////////////////
//
// Start
//
bool AppInit(int argc, char* argv[]) {
  CScheduler scheduler;

  bool fRet = false;

  //
  // Parameters
  //
  // If Qt is used, parameters/tessa.conf are parsed in qt/tessa.cpp's main()
  ParseParameters(argc, argv);

  // Process help and version before taking care about datadir
  if (gArgs.IsArgSet("-?") || gArgs.IsArgSet("-help") || gArgs.IsArgSet("-version")) {
    std::string strUsage = _("Tessa Core Daemon") + " " + _("version") + " " + FormatFullVersion() + "\n";

    if (gArgs.IsArgSet("-version")) {
      strUsage += LicenseInfo();
    } else {
      strUsage +=
          "\n" + _("Usage:") + "\n" + "  tessad [options]                     " + _("Start Tessa Core Daemon") + "\n";

      strUsage += "\n" + HelpMessage(HMM_BITCOIND);
    }

    fprintf(stdout, "%s", strUsage.c_str());
    return false;
  }

  try {
    if (!fs::is_directory(GetDataDir(false))) {
      fprintf(stderr, "Error: Specified data directory \"%s\" does not exist.\n", gArgs.GetArg("-datadir", "").c_str());
      return false;
    }
    try {
      ReadConfigFile();
    } catch (std::exception& e) {
      fprintf(stderr, "Error reading configuration file: %s\n", e.what());
      return false;
    }
    // Check for -testnet or -regtest parameter (Params() calls are only valid after this clause)
    if (!SelectParamsFromCommandLine()) {
      fprintf(stderr, "Error: Invalid combination of -regtest and -testnet.\n");
      return false;
    }

    // Command-line RPC
    bool fCommandLine = false;
    for (int i = 1; i < argc; i++) {
      std::string arv = argv[i];
      if (!IsSwitchChar(argv[i][0]) && !(arv.substr(0, 5) == "tessa:")) fCommandLine = true;
    }
    if (fCommandLine) {
      fprintf(stderr,
              "Error: There is no RPC client functionality in tessad anymore. Use the tessa-cli utility instead.\n");
      exit(1);
    }
#ifndef WIN32
    fDaemon = GetBoolArg("-daemon", false);
    if (fDaemon) {
      fprintf(stdout, "Tessa server starting\n");

      // Daemonize
      pid_t pid = fork();
      if (pid < 0) {
        fprintf(stderr, "Error: fork() returned %d errno %d\n", pid, errno);
        return false;
      }
      if (pid > 0)  // Parent process, pid is child process id
      {
        return true;
      }
      // Child process falls through to rest of initialization

      pid_t sid = setsid();
      if (sid < 0) fprintf(stderr, "Error: setsid() returned %d errno %d\n", sid, errno);
    }
#endif
    SoftSetBoolArg("-server", true);

    InitLogging();
    fRet = AppInit2(scheduler);
  } catch (std::exception& e) { PrintExceptionContinue(&e, "AppInit()"); } catch (...) {
    PrintExceptionContinue(nullptr, "AppInit()");
  }

  if (!fRet) {
    Interrupt(scheduler);
    // threadGroup.join_all(); was left out intentionally here, because we didn't re-test all of
    // the startup-failure cases to make sure they don't result in a hang due to some
    // thread-blocking-waiting-for-another-thread-during-startup case
  } else {
    WaitForShutdown(scheduler);
  }
  Shutdown(scheduler);

  return fRet;
}

int main(int argc, char* argv[]) {
  SetupEnvironment();

  // Connect tessad signal handlers
  noui_connect();

  return (AppInit(argc, argv) ? 0 : 1);
}
