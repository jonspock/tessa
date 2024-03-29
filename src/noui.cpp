// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2017 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "noui.h"

#include "ui_interface.h"
#include "util.h"

#include <cstdint>
#include <cstdio>
#include <string>

#include "signals-cpp/signals.hpp"

static void noui_ThreadSafeMessageBox(const std::string& message, const std::string& caption, uint32_t style, bool* b) {
  bool fSecure = style & CClientUIInterface::SECURE;
  style &= ~CClientUIInterface::SECURE;

  std::string strCaption;
  // Check for usage of predefined caption
  switch (style) {
    case CClientUIInterface::MSG_ERROR:
      strCaption += _("Error");
      break;
    case CClientUIInterface::MSG_WARNING:
      strCaption += _("Warning");
      break;
    case CClientUIInterface::MSG_INFORMATION:
      strCaption += _("Information");
      break;
    default:
      strCaption += caption;  // Use supplied caption (can be empty)
  }

  if (!fSecure) LogPrintf("%s: %s\n", strCaption, message);
  fprintf(stderr, "%s: %s\n", strCaption.c_str(), message.c_str());
  *b = false;
}

static void noui_InitMessage(const std::string& message) { LogPrintf("init message: %s\n", message); }

void noui_connect() {
  // Connect tessad signal handlers
  uiInterface.ThreadSafeMessageBox.connect(noui_ThreadSafeMessageBox);
  uiInterface.InitMessage.connect(noui_InitMessage);
}
