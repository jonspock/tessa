// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include "externs.h"
#include <string>
class CScheduler;

void StartShutdown();
bool ShutdownRequested();
/** Interrupt threads */
void InterruptSearch();  // Rpc Zkp search
void Interrupt(CScheduler& scheduler);
void Shutdown(CScheduler& scheduler);
void PrepareShutdown(CScheduler& scheduler);
void InitLogging();
bool AppInit2(CScheduler& scheduler);

/** The help message mode determines what help message to show */
enum HelpMessageMode { HMM_BITCOIND, HMM_BITCOIN_QT };

/** Help for options shared between UI and daemon (for -help) */
std::string HelpMessage(HelpMessageMode mode);
/** Returns licensing information (for -version) */
std::string LicenseInfo();
