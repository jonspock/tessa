// Copyright (c) 2011-2014 The Bitcoin developers
// Copyright (c) 2014-2015 The Dash developers
// Copyright (c) 2015-2018 The PIVX developers
// Copyright (c) 2018 The Tessacoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "clientmodel.h"
#include "bantablemodel.h"
#include "chain.h"
#include "externs.h"
#include "guiconstants.h"
#include "peertablemodel.h"

#include "chainparams.h"
#include "checkpoints.h"
#include "clientversion.h"
#include "main.h"
#include "net.h"
#include "ui_interface.h"
#include "util.h"
#include "utiltime.h"

#include <stdint.h>

#include <QDateTime>
#include <QDebug>
#include <QTimer>

static const int64_t nClientStartupTime = GetTime();

ClientModel::ClientModel(OptionsModel* optionsModel, QObject* parent)
    : QObject(parent),
      optionsModel(optionsModel),
      peerTableModel(0),
      banTableModel(0),
      cachedNumBlocks(0),
      cachedReindexing(false),
      cachedImporting(false),
      numBlocksAtStartup(-1),
      pollTimer(0) {
  peerTableModel = new PeerTableModel(this);
  banTableModel = new BanTableModel(this);
  pollTimer = new QTimer(this);
  connect(pollTimer, SIGNAL(timeout()), this, SLOT(updateTimer()));
  pollTimer->start(MODEL_UPDATE_DELAY);

  pollMnTimer = new QTimer(this);
  connect(pollMnTimer, SIGNAL(timeout()), this, SLOT(updateMnTimer()));
  // no need to update as frequent as data for balances/txes/blocks
  pollMnTimer->start(MODEL_UPDATE_DELAY * 4);

  subscribeToCoreSignals();
}

ClientModel::~ClientModel() { unsubscribeFromCoreSignals(); }

int ClientModel::getNumConnections(unsigned int flags) const {
  LOCK(cs_vNodes);
  if (flags == CONNECTIONS_ALL)  // Shortcut if we want total
    return vNodes.size();

  int nNum = 0;
  for (CNode* pnode : vNodes)
    if (flags & (pnode->fInbound ? CONNECTIONS_IN : CONNECTIONS_OUT)) nNum++;

  return nNum;
}

int ClientModel::getNumBlocks() const {
  LOCK(cs_main);
  return chainActive.Height();
}

int ClientModel::getNumBlocksAtStartup() {
  if (numBlocksAtStartup == -1) numBlocksAtStartup = getNumBlocks();
  return numBlocksAtStartup;
}

quint64 ClientModel::getTotalBytesRecv() const { return CNode::GetTotalBytesRecv(); }

quint64 ClientModel::getTotalBytesSent() const { return CNode::GetTotalBytesSent(); }

QDateTime ClientModel::getLastBlockDate() const {
  LOCK(cs_main);
  if (chainActive.Tip())
    return QDateTime::fromTime_t(chainActive.Tip()->GetBlockTime());
  else
    return QDateTime::fromTime_t(Params().GenesisBlock().GetBlockTime());  // Genesis block's time of current network
}

double ClientModel::getVerificationProgress() const {
  LOCK(cs_main);
  return Checkpoints::GuessVerificationProgress(chainActive.Tip());
}

void ClientModel::updateTimer() {
  // Get required lock upfront. This avoids the GUI from getting stuck on
  // periodical polls if the core is holding the locks for a longer time -
  // for example, during a wallet rescan.
  TRY_LOCK(cs_main, lockMain);
  if (!lockMain) return;
  emit bytesChanged(getTotalBytesRecv(), getTotalBytesSent());
}

void ClientModel::updateMnTimer() {}

void ClientModel::updateNumConnections(int numConnections) { emit numConnectionsChanged(numConnections); }

bool ClientModel::inInitialBlockDownload() const { return IsInitialBlockDownload(); }

enum BlockSource ClientModel::getBlockSource() const {
  if (fReindex)
    return BLOCK_SOURCE_REINDEX;
  else if (fImporting)
    return BLOCK_SOURCE_DISK;
  else if (getNumConnections() > 0)
    return BLOCK_SOURCE_NETWORK;

  return BLOCK_SOURCE_NONE;
}

QString ClientModel::getStatusBarWarnings() const { return QString::fromStdString(GetWarnings("statusbar")); }

OptionsModel* ClientModel::getOptionsModel() { return optionsModel; }

PeerTableModel* ClientModel::getPeerTableModel() { return peerTableModel; }

BanTableModel* ClientModel::getBanTableModel() { return banTableModel; }

QString ClientModel::formatFullVersion() const { return QString::fromStdString(FormatFullVersion()); }

QString ClientModel::formatBuildDate() const { return QString::fromStdString(CLIENT_DATE); }

QString ClientModel::clientName() const { return QString::fromStdString(CLIENT_NAME); }

QString ClientModel::formatClientStartupTime() const { return QDateTime::fromTime_t(nClientStartupTime).toString(); }

void ClientModel::updateBanlist() { banTableModel->refresh(); }

// Handlers for core signals
static void ShowProgress(ClientModel* clientmodel, const std::string& title, int nProgress) {
  // emits signal "showProgress"
  QMetaObject::invokeMethod(clientmodel, "showProgress", Qt::QueuedConnection,
                            Q_ARG(QString, QString::fromStdString(title)), Q_ARG(int, nProgress));
}

static void NotifyNumConnectionsChanged(ClientModel* clientmodel, int newNumConnections) {
  // Too noisy: qDebug() << "NotifyNumConnectionsChanged : " + QString::number(newNumConnections);
  QMetaObject::invokeMethod(clientmodel, "updateNumConnections", Qt::QueuedConnection, Q_ARG(int, newNumConnections));
}

static void BannedListChanged(ClientModel* clientmodel) {
  qDebug() << QString("%1: Requesting update for peer banlist").arg(__func__);
  QMetaObject::invokeMethod(clientmodel, "updateBanlist", Qt::QueuedConnection);
}

void ClientModel::subscribeToCoreSignals() {
  // Connect signals to client
  uiInterface.ShowProgress_connect(boost::bind(ShowProgress, this, _1, _2));
  uiInterface.NotifyNumConnectionsChanged_connect(boost::bind(NotifyNumConnectionsChanged, this, _1));
  uiInterface.BannedListChanged_connect(boost::bind(BannedListChanged, this));
}

void ClientModel::unsubscribeFromCoreSignals() {
  // Disconnect signals from client
  uiInterface.ShowProgress_disconnect(boost::bind(ShowProgress, this, _1, _2));
  uiInterface.NotifyNumConnectionsChanged_disconnect(boost::bind(NotifyNumConnectionsChanged, this, _1));
  uiInterface.BannedListChanged_disconnect(boost::bind(BannedListChanged, this));
}
