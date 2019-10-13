// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "validationinterface.h"

#include <boost/signals2/last_value.hpp>
#include <boost/signals2/signal.hpp>

static CMainSignals g_signals;

CMainSignals& GetMainSignals() { return g_signals; }

void RegisterValidationInterface(CValidationInterface* pwalletIn) {
  g_signals.UpdatedBlockTip_connect(boost::bind(&CValidationInterface::UpdatedBlockTip, pwalletIn, _1));
  g_signals.SyncTransaction_connect(boost::bind(&CValidationInterface::SyncTransaction, pwalletIn, _1, _2));
  g_signals.NotifyTransactionLock_connect(boost::bind(&CValidationInterface::NotifyTransactionLock, pwalletIn, _1));
  g_signals.UpdatedTransaction_connect(boost::bind(&CValidationInterface::UpdatedTransaction, pwalletIn, _1));
  g_signals.SetBestChain_connect(boost::bind(&CValidationInterface::SetBestChain, pwalletIn, _1));
  g_signals.Inventory_connect(boost::bind(&CValidationInterface::Inventory, pwalletIn, _1));
  g_signals.Broadcast_connect(boost::bind(&CValidationInterface::ResendWalletTransactions, pwalletIn));
  g_signals.BlockChecked_connect(boost::bind(&CValidationInterface::BlockChecked, pwalletIn, _1, _2));
  g_signals.BlockFound_connect(boost::bind(&CValidationInterface::ResetRequestCount, pwalletIn, _1));
}

void UnregisterValidationInterface(CValidationInterface* pwalletIn) {
  g_signals.BlockFound_disconnect(boost::bind(&CValidationInterface::ResetRequestCount, pwalletIn, _1));
  g_signals.BlockChecked_disconnect(boost::bind(&CValidationInterface::BlockChecked, pwalletIn, _1, _2));
  g_signals.Broadcast_disconnect(boost::bind(&CValidationInterface::ResendWalletTransactions, pwalletIn));
  g_signals.Inventory_disconnect(boost::bind(&CValidationInterface::Inventory, pwalletIn, _1));
  g_signals.SetBestChain_disconnect(boost::bind(&CValidationInterface::SetBestChain, pwalletIn, _1));
  g_signals.UpdatedTransaction_disconnect(boost::bind(&CValidationInterface::UpdatedTransaction, pwalletIn, _1));
  g_signals.NotifyTransactionLock_disconnect(boost::bind(&CValidationInterface::NotifyTransactionLock, pwalletIn, _1));
  g_signals.SyncTransaction_disconnect(boost::bind(&CValidationInterface::SyncTransaction, pwalletIn, _1, _2));
  g_signals.UpdatedBlockTip_disconnect(boost::bind(&CValidationInterface::UpdatedBlockTip, pwalletIn, _1));
}

void UnregisterAllValidationInterfaces() {
  g_signals.BlockFound_disconnect_all_slots();
  g_signals.BlockChecked_disconnect_all_slots();
  g_signals.Broadcast_disconnect_all_slots();
  g_signals.Inventory_disconnect_all_slots();
  g_signals.SetBestChain_disconnect_all_slots();
  g_signals.UpdatedTransaction_disconnect_all_slots();
  g_signals.NotifyTransactionLock_disconnect_all_slots();
  g_signals.SyncTransaction_disconnect_all_slots();
  g_signals.UpdatedBlockTip_disconnect_all_slots();
}

void SyncWithWallets(const CTransaction& tx, const CBlock* pblock = nullptr) { g_signals.SyncTransaction(tx, pblock); }

struct CMainSignalSigs {
  boost::signals2::signal<CMainSignals::UpdatedBlockTipSig> UpdatedBlockTip;
  boost::signals2::signal<CMainSignals::SyncTransactionSig> SyncTransaction;
  boost::signals2::signal<CMainSignals::NotifyTransactionLockSig> NotifyTransactionLock;
  boost::signals2::signal<CMainSignals::SetBestChainSig> SetBestChain;
  boost::signals2::signal<CMainSignals::InventorySig> Inventory;
  boost::signals2::signal<CMainSignals::BroadcastSig> Broadcast;
  boost::signals2::signal<CMainSignals::BlockCheckedSig> BlockChecked;
  boost::signals2::signal<CMainSignals::BlockFoundSig> BlockFound;
  boost::signals2::signal<CMainSignals::UpdatedTransactionSig, boost::signals2::last_value<bool> > UpdatedTransaction;
} g_main_signals;

#define ADD_SIGNALS_IMPL_WRAPPER(signal_name)                                                           \
  boost::signals2::connection CMainSignals::signal_name##_connect(std::function<signal_name##Sig> fn) { \
    return g_main_signals.signal_name.connect(fn);                                                      \
  }                                                                                                     \
  void CMainSignals::signal_name##_disconnect(std::function<signal_name##Sig> fn) {                     \
    return g_main_signals.signal_name.disconnect(&fn);                                                  \
  }                                                                                                     \
  void CMainSignals::signal_name##_disconnect_all_slots() { return g_main_signals.signal_name.disconnect_all_slots(); }

ADD_SIGNALS_IMPL_WRAPPER(UpdatedBlockTip);
ADD_SIGNALS_IMPL_WRAPPER(SyncTransaction);
ADD_SIGNALS_IMPL_WRAPPER(NotifyTransactionLock);
ADD_SIGNALS_IMPL_WRAPPER(SetBestChain);
ADD_SIGNALS_IMPL_WRAPPER(Inventory);
ADD_SIGNALS_IMPL_WRAPPER(Broadcast);
ADD_SIGNALS_IMPL_WRAPPER(BlockChecked);
ADD_SIGNALS_IMPL_WRAPPER(BlockFound);
ADD_SIGNALS_IMPL_WRAPPER(UpdatedTransaction);

void CMainSignals::UpdatedBlockTip(const CBlockIndex* c) { g_main_signals.UpdatedBlockTip(c); }

void CMainSignals::SyncTransaction(const CTransaction& t, const CBlock* b) { g_main_signals.SyncTransaction(t, b); }
void CMainSignals::NotifyTransactionLock(const CTransaction& t) { g_main_signals.NotifyTransactionLock(t); }
void CMainSignals::SetBestChain(const CBlockLocator& u) { g_main_signals.SetBestChain(u); }

void CMainSignals::Inventory(const uint256& u) { g_main_signals.Inventory(u); }
void CMainSignals::Broadcast() { g_main_signals.Broadcast(); }
void CMainSignals::BlockChecked(const CBlock& b, const CValidationState& v) { g_main_signals.BlockChecked(b, v); }
void CMainSignals::BlockFound(const uint256& u) { g_main_signals.BlockFound(u); }
bool CMainSignals::UpdatedTransaction(const uint256& u) { return g_main_signals.UpdatedTransaction(u); }
