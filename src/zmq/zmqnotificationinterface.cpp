// Copyright (c) 2015-2017 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "zmqnotificationinterface.h"
#include "zmqpublishnotifier.h"

#include "version.h"
#include "main.h"
#include "streams.h"
#include "util.h"

void zmqError(const char *str)
{
    LogPrint(TessaLog::ZMQ, "zmq: Error: %s, errno=%s\n", str, zmq_strerror(errno));
}

CZMQNotificationInterface::CZMQNotificationInterface() : pcontext(nullptr)
{
}

CZMQNotificationInterface::~CZMQNotificationInterface()
{
    Shutdown();

    for (std::list<CZMQAbstractNotifier*>::iterator i=notifiers.begin(); i!=notifiers.end(); ++i)
    {
        delete *i;
    }
}

CZMQNotificationInterface* CZMQNotificationInterface::Create()
{
    CZMQNotificationInterface* notificationInterface = nullptr;
    std::map<std::string, CZMQNotifierFactory> factories;
    std::list<CZMQAbstractNotifier*> notifiers;

    factories["pubhashblock"] = CZMQAbstractNotifier::Create<CZMQPublishHashBlockNotifier>;
    factories["pubhashtx"] = CZMQAbstractNotifier::Create<CZMQPublishHashTransactionNotifier>;
    factories["pubrawblock"] = CZMQAbstractNotifier::Create<CZMQPublishRawBlockNotifier>;
    factories["pubrawtx"] = CZMQAbstractNotifier::Create<CZMQPublishRawTransactionNotifier>;

    for (const auto& entry : factories)
    {
        std::string arg("-zmq" + entry.first);
        if (gArgs.IsArgSet(arg))
        {
            CZMQNotifierFactory factory = entry.second;
            std::string address = gArgs.GetArg(arg, "");
            CZMQAbstractNotifier *notifier = factory();
            notifier->SetType(entry.first);
            notifier->SetAddress(address);
            notifiers.push_back(notifier);
        }
    }

    if (!notifiers.empty())
    {
        notificationInterface = new CZMQNotificationInterface();
        notificationInterface->notifiers = notifiers;

        if (!notificationInterface->Initialize())
        {
            delete notificationInterface;
            notificationInterface = nullptr;
        }
    }

    return notificationInterface;
}

// Called at startup to conditionally set up ZMQ socket(s)
bool CZMQNotificationInterface::Initialize()
{
    LogPrint(TessaLog::ZMQ, "zmq: Initialize notification interface\n");
    assert(!pcontext);

    pcontext = zmq_init(1);

    if (!pcontext)
    {
        zmqError("Unable to initialize context");
        return false;
    }

    std::list<CZMQAbstractNotifier*>::iterator i=notifiers.begin();
    for (; i!=notifiers.end(); ++i)
    {
        CZMQAbstractNotifier *notifier = *i;
        if (notifier->Initialize(pcontext))
        {
            LogPrint(TessaLog::ZMQ, "  Notifier %s ready (address = %s)\n", notifier->GetType(), notifier->GetAddress());
        }
        else
        {
            LogPrint(TessaLog::ZMQ, "  Notifier %s failed (address = %s)\n", notifier->GetType(), notifier->GetAddress());
            break;
        }
    }

    if (i!=notifiers.end())
    {
        return false;
    }

    return true;
}

// Called during shutdown sequence
void CZMQNotificationInterface::Shutdown()
{
    LogPrint(TessaLog::ZMQ, "zmq: Shutdown notification interface\n");
    if (pcontext)
    {
        for (std::list<CZMQAbstractNotifier*>::iterator i=notifiers.begin(); i!=notifiers.end(); ++i)
        {
            CZMQAbstractNotifier *notifier = *i;
            LogPrint(TessaLog::ZMQ, "   Shutdown notifier %s at %s\n", notifier->GetType(), notifier->GetAddress());
            notifier->Shutdown();
        }
        zmq_ctx_destroy(pcontext);

        pcontext = 0;
    }
}

void CZMQNotificationInterface::UpdatedBlockTip(const CBlockIndex *pindex)
{
    for (std::list<CZMQAbstractNotifier*>::iterator i = notifiers.begin(); i!=notifiers.end(); )
    {
        CZMQAbstractNotifier *notifier = *i;
        if (notifier->NotifyBlock(pindex))
        {
            i++;
        }
        else
        {
            notifier->Shutdown();
            i = notifiers.erase(i);
        }
    }
}

void CZMQNotificationInterface::SyncTransaction(const CTransaction &tx, const CBlock *pblock)
{
    for (std::list<CZMQAbstractNotifier*>::iterator i = notifiers.begin(); i!=notifiers.end(); )
    {
        CZMQAbstractNotifier *notifier = *i;
        if (notifier->NotifyTransaction(tx))
        {
            i++;
        }
        else
        {
            notifier->Shutdown();
            i = notifiers.erase(i);
        }
    }
}

void CZMQNotificationInterface::NotifyTransactionLock(const CTransaction &tx)
{
    for (std::list<CZMQAbstractNotifier*>::iterator i = notifiers.begin(); i!=notifiers.end(); )
    {
        CZMQAbstractNotifier *notifier = *i;
        if (notifier->NotifyTransactionLock(tx))
        {
            i++;
        }
        else
        {
            notifier->Shutdown();
            i = notifiers.erase(i);
        }
    }
}
