// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2015 The Bitcoin Core developers
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 The TessaChain developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#pragma once

#include <functional>

namespace boost {
namespace signals2 {
class connection;
}
}  // namespace boost

class CBlock;
struct CBlockLocator;
class CBlockIndex;
class CReserveScript;
class CTransaction;
class CValidationInterface;
class CValidationState;
class uint256;

// These functions dispatch to one or all registered wallets

/** Register a wallet to receive updates from core */
void RegisterValidationInterface(CValidationInterface *pwalletIn);
/** Unregister a wallet from core */
void UnregisterValidationInterface(CValidationInterface *pwalletIn);
/** Unregister all wallets from core */
void UnregisterAllValidationInterfaces();
/** Push an updated transaction to all registered wallets */
void SyncWithWallets(const CTransaction &tx, const CBlock *pblock);

class CValidationInterface {
 protected:
  virtual void UpdatedBlockTip(const CBlockIndex *pindex) {}
  virtual void SyncTransaction(const CTransaction &tx, const CBlock *pblock) {}
  virtual void NotifyTransactionLock(const CTransaction &tx) {}
  virtual void SetBestChain(const CBlockLocator &locator) {}
  virtual bool UpdatedTransaction(const uint256 &hash) { return false; }
  virtual void Inventory(const uint256 &hash) {}
  virtual void ResendWalletTransactions() {}
  virtual void BlockChecked(const CBlock &, const CValidationState &) {}
  virtual void ResetRequestCount(const uint256 &hash){};
  friend void ::RegisterValidationInterface(CValidationInterface *);
  friend void ::UnregisterValidationInterface(CValidationInterface *);
  friend void ::UnregisterAllValidationInterfaces();
};

#define ADD_VAL_SIGNALS_DECL_WRAPPER(signal_name, rtype, ...)                            \
  rtype signal_name(__VA_ARGS__);                                                        \
  using signal_name##Sig = rtype(__VA_ARGS__);                                           \
  boost::signals2::connection signal_name##_connect(std::function<signal_name##Sig> fn); \
  void signal_name##_disconnect_all_slots();                                             \
  void signal_name##_disconnect(std::function<signal_name##Sig> fn);

// Signals for message handling
struct CMainSignals {
  // Notifies listeners of updated block chain tip
  ADD_VAL_SIGNALS_DECL_WRAPPER(UpdatedBlockTip, void, const CBlockIndex *)
  // Notifies listeners of updated transaction data (transaction, and optionally the block it is found in.
  ADD_VAL_SIGNALS_DECL_WRAPPER(SyncTransaction, void, const CTransaction &, const CBlock *)
  // Notifies listeners of an updated transaction lock without new data.
  ADD_VAL_SIGNALS_DECL_WRAPPER(NotifyTransactionLock, void, const CTransaction &)
  // Notifies listeners of an updated transaction without new data (for now: a coinbase potentially becoming visible).
  ADD_VAL_SIGNALS_DECL_WRAPPER(UpdatedTransaction, bool, const uint256 &)
  // Notifies listeners of a new active block chain.
  ADD_VAL_SIGNALS_DECL_WRAPPER(SetBestChain, void, const CBlockLocator &)
  // Notifies listeners about an inventory item being seen on the network.
  ADD_VAL_SIGNALS_DECL_WRAPPER(Inventory, void, const uint256 &)
  // Tells listeners to broadcast their data.
  ADD_VAL_SIGNALS_DECL_WRAPPER(Broadcast, void, void)
  // Notifies listeners of a block validation result
  ADD_VAL_SIGNALS_DECL_WRAPPER(BlockChecked, void, const CBlock &, const CValidationState &)
  // Notifies listeners that a block has been successfully mined
  ADD_VAL_SIGNALS_DECL_WRAPPER(BlockFound, void, const uint256 &)
};

CMainSignals &GetMainSignals();
