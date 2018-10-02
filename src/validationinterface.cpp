// Copyright (c) 2009-2010 Satoshi Nakamoto
// Copyright (c) 2009-2014 The Bitcoin Core developers
// Copyright (c) 2017 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "validationinterface.h"

static CMainSignals g_signals;

CMainSignals& GetMainSignals() { return g_signals; }

void RegisterValidationInterface(CValidationInterface* pwalletIn) {
  g_signals.UpdatedBlockTip.connect(
      std::bind(&CValidationInterface::UpdatedBlockTip, pwalletIn, std::placeholders::_1));
  g_signals.SyncTransaction.connect(
      std::bind(&CValidationInterface::SyncTransaction, pwalletIn, std::placeholders::_1, std::placeholders::_2));
  g_signals.NotifyTransactionLock.connect(
      std::bind(&CValidationInterface::NotifyTransactionLock, pwalletIn, std::placeholders::_1));
  g_signals.UpdatedTransaction.connect(
      std::bind(&CValidationInterface::UpdatedTransaction, pwalletIn, std::placeholders::_1));
  g_signals.SetBestChain.connect(std::bind(&CValidationInterface::SetBestChain, pwalletIn, std::placeholders::_1));
  g_signals.Inventory.connect(std::bind(&CValidationInterface::Inventory, pwalletIn, std::placeholders::_1));
  g_signals.Broadcast.connect(std::bind(&CValidationInterface::ResendWalletTransactions, pwalletIn));
  g_signals.BlockChecked.connect(
      std::bind(&CValidationInterface::BlockChecked, pwalletIn, std::placeholders::_1, std::placeholders::_2));
  g_signals.BlockFound.connect(std::bind(&CValidationInterface::ResetRequestCount, pwalletIn, std::placeholders::_1));
}

void UnregisterValidationInterface(CValidationInterface* pwalletIn) {}

void UnregisterAllValidationInterfaces() {}

void SyncWithWallets(const CTransaction& tx, const CBlock* pblock = nullptr) {
  g_signals.SyncTransaction.fire(tx, pblock);
}
