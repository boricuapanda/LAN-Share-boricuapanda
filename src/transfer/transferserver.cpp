/*
    LANShare - LAN file transfer.
    Copyright (C) 2016 Abdul Aris R. <abdularisrahmanudin10@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include "transferserver.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSslSocket>

#include "log.h"
#include "settings.h"
#include "tlshelper.h"
#include "model/transferfailure.h"
#include "transfer/transfer.h"

namespace {

class TransferTcpServer : public QTcpServer
{
public:
    explicit TransferTcpServer(QObject* parent = nullptr) : QTcpServer(parent) {}

protected:
    void incomingConnection(qintptr socketDescriptor) override
    {
        auto* socket = new QSslSocket(this);
        if (!socket->setSocketDescriptor(socketDescriptor)) {
            socket->deleteLater();
            return;
        }
        addPendingConnection(socket);
    }
};

} // namespace

TransferServer::TransferServer(DeviceListModel* devList, QObject *parent) : QObject(parent)
{
    mDevList = devList;
    mServer = new TransferTcpServer(this);
    connect(mServer, &QTcpServer::newConnection, this, &TransferServer::onNewConnection);
}

bool TransferServer::listen(const QHostAddress &addr)
{
    return mServer->listen(addr, Settings::instance()->getTransferPort());
}

int TransferServer::activeDownloadCount() const
{
    int count = 0;
    for (Receiver* receiver : mReceivers) {
        if (isActiveReceiver(receiver))
            ++count;
    }
    return count;
}

bool TransferServer::isActiveReceiver(const Receiver* receiver) const
{
    if (!receiver)
        return false;

    const TransferState state = receiver->getTransferInfo()->getState();
    return state == TransferState::Waiting
           || state == TransferState::Transfering
           || state == TransferState::Paused;
}

void TransferServer::rejectBusy(QTcpSocket* socket)
{
    if (!socket)
        return;

    const int retryAfterMs = Settings::instance()->getTransferRetryBaseMs();
    const QJsonObject obj({
        {QStringLiteral("busy"), true},
        {QStringLiteral("retry_after_ms"), retryAfterMs}
    });
    const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    const qint32 packetSize = payload.size();
    const char type = static_cast<char>(PacketType::OffsetAck);
    socket->write(reinterpret_cast<const char*>(&packetSize), sizeof(packetSize));
    socket->write(&type, sizeof(type));
    socket->write(payload);
    socket->flush();
    socket->waitForBytesWritten(1000);
    AppLog::transferEvent(QStringLiteral("-"),
                          QStringLiteral("admission_busy"),
                          socket->peerAddress().toString(),
                          QStringLiteral("admission_busy"),
                          QStringLiteral("0"),
                          tr("Rejected incoming transfer: download cap reached."));
    socket->disconnectFromHost();
    socket->deleteLater();
}

void TransferServer::tryStartPending()
{
    const int maxDownloads = Settings::instance()->getMaxConcurrentDownloads();
    while (!mPendingSockets.isEmpty()) {
        if (maxDownloads > 0 && activeDownloadCount() >= maxDownloads)
            break;
        QTcpSocket* socket = mPendingSockets.dequeue();
        if (!socket)
            continue;
        startReceiver(socket);
    }
}

void TransferServer::onNewConnection()
{
    QTcpSocket* socket = mServer->nextPendingConnection();
    if (!socket)
        return;

    const int maxDownloads = Settings::instance()->getMaxConcurrentDownloads();
    if (maxDownloads > 0 && activeDownloadCount() >= maxDownloads) {
        rejectBusy(socket);
        return;
    }

    startReceiver(socket);
}

void TransferServer::startReceiver(QTcpSocket* socket)
{
    if (!socket)
        return;

    Device dev = mDevList->device(socket->peerAddress());
    Receiver* rec = new Receiver(dev, socket);
    mReceivers.push_back(rec);
    connect(rec->getTransferInfo(), &TransferInfo::fileOpened, this, [this, rec]() {
        emit newReceiverAdded(rec);
    });
    connect(rec->getTransferInfo(), &TransferInfo::done, this, &TransferServer::onReceiverFinished);
    connect(rec->getTransferInfo(), &TransferInfo::errorOcurred, this, &TransferServer::onReceiverFinished);

    if (Settings::instance()->getTlsEnabled()) {
        auto* sslSocket = qobject_cast<QSslSocket*>(socket);
        if (!sslSocket) {
            rec->getTransferInfo()->fail(TransferFailureReason::TlsError,
                                         tr("TLS setup failed: secure socket is unavailable."));
            socket->disconnectFromHost();
            return;
        }

        if (!mTlsCredentialsReady) {
            QString tlsError;
            mTlsCredentialsReady = TlsHelper::loadServerCredentials(&mServerCert, &mServerKey, &tlsError);
            if (!mTlsCredentialsReady) {
                AppLog::write("tls", tlsError);
                rec->getTransferInfo()->fail(TransferFailureReason::TlsError, tlsError);
            }
        }

        if (!mTlsCredentialsReady) {
            sslSocket->disconnectFromHost();
            return;
        }

        sslSocket->setLocalCertificate(mServerCert);
        sslSocket->setPrivateKey(mServerKey);
        sslSocket->setPeerVerifyMode(QSslSocket::VerifyNone);
        sslSocket->startServerEncryption();
    }
}

void TransferServer::onReceiverFinished()
{
    tryStartPending();
}
