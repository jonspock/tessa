// Copyright (c) 2010 Satoshi Nakamoto
// Copyright (c) 2012-2014 The Bitcoin developers
// Copyright (c) 2017-2018 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <functional>
#include <memory>
#include <cstdint>
#include <string>

namespace boost {
namespace signals2 {
class connection;
}
} // namespace boost

class CBlockIndex;
class CBasicKeyStore;
class CWallet;
class uint256;

/** General change type (added, updated, removed). */
enum ChangeType { CT_NEW, CT_UPDATED, CT_DELETED };

/** Signals for UI communication. */
class CClientUIInterface {
 public:
  /** Flags for CClientUIInterface::ThreadSafeMessageBox */
  enum MessageBoxFlags {
    ICON_INFORMATION = 0,
    ICON_WARNING = (1U << 0),
    ICON_ERROR = (1U << 1),
    /**
     * Mask of all available icons in CClientUIInterface::MessageBoxFlags
     * This needs to be updated, when icons are changed there!
     */
    ICON_MASK = (ICON_INFORMATION | ICON_WARNING | ICON_ERROR),

    /** These values are taken from qmessagebox.h "enum StandardButton" to be directly usable */
    BTN_OK = 0x00000400U,       // QMessageBox::Ok
    BTN_YES = 0x00004000U,      // QMessageBox::Yes
    BTN_NO = 0x00010000U,       // QMessageBox::No
    BTN_ABORT = 0x00040000U,    // QMessageBox::Abort
    BTN_RETRY = 0x00080000U,    // QMessageBox::Retry
    BTN_IGNORE = 0x00100000U,   // QMessageBox::Ignore
    BTN_CLOSE = 0x00200000U,    // QMessageBox::Close
    BTN_CANCEL = 0x00400000U,   // QMessageBox::Cancel
    BTN_DISCARD = 0x00800000U,  // QMessageBox::Discard
    BTN_HELP = 0x01000000U,     // QMessageBox::Help
    BTN_APPLY = 0x02000000U,    // QMessageBox::Apply
    BTN_RESET = 0x04000000U,    // QMessageBox::Reset
    /**
     * Mask of all available buttons in CClientUIInterface::MessageBoxFlags
     * This needs to be updated, when buttons are changed there!
     */
    BTN_MASK = (BTN_OK | BTN_YES | BTN_NO | BTN_ABORT | BTN_RETRY | BTN_IGNORE | BTN_CLOSE | BTN_CANCEL | BTN_DISCARD |
                BTN_HELP | BTN_APPLY | BTN_RESET),

    /** Force blocking, modal message box dialog (not just OS notification) */
    MODAL = 0x10000000U,

    /** Do not print contents of message to debug log */
    SECURE = 0x40000000U,

    /** Predefined combinations for certain default usage cases */
    MSG_INFORMATION = ICON_INFORMATION,
    MSG_WARNING = (ICON_WARNING | BTN_OK | MODAL),
    MSG_ERROR = (ICON_ERROR | BTN_OK | MODAL)
  };

#define ADD_SIGNALS_DECL_WRAPPER(signal_name, rtype, ...)                                  \
    rtype signal_name(__VA_ARGS__);                                                        \
    using signal_name##Sig = rtype(__VA_ARGS__);                                           \
    boost::signals2::connection signal_name##_connect(std::function<signal_name##Sig> fn); \
    void signal_name##_disconnect(std::function<signal_name##Sig> fn);

  
  /** Show message box. */
  ADD_SIGNALS_DECL_WRAPPER(ThreadSafeMessageBox, bool, const std::string& message, const std::string& caption, unsigned int style)

  /** Progress message during initialization. */
  ADD_SIGNALS_DECL_WRAPPER(InitMessage, void, const std::string& message)
  
    /** Translate a message to the native language of the user. */
  //boost::signals2::signal<std::string(const char* psz)> Translate;
  ///// ADD_SIGNALS_DECL_WRAPPER(Translate, std::string, const char* psz)

  /** Number of network connections changed. */
  ADD_SIGNALS_DECL_WRAPPER(NotifyNumConnectionsChanged, void, int newNumConnections)

  /** A wallet has been loaded. */
  ADD_SIGNALS_DECL_WRAPPER(LoadWallet, void, CWallet* wallet)

  /** Show progress e.g. for verifychain */
  ADD_SIGNALS_DECL_WRAPPER(ShowProgress, void, const std::string& title, int nProgress)//, bool resume_possible);

  /** New block has been accepted */
  ADD_SIGNALS_DECL_WRAPPER(NotifyBlockTip, void, const uint256&)

  /** New block has been accepted and is over a certain size */
  ADD_SIGNALS_DECL_WRAPPER(NotifyBlockSize, void, int, const uint256&)

  /** Banlist did change. */
  ADD_SIGNALS_DECL_WRAPPER(BannedListChanged, void, void)
};

extern CClientUIInterface uiInterface;
