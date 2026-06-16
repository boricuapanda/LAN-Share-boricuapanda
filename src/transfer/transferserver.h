/*
    LANShare - LAN file transfer.
    Copyright (C) 2016 Abdul Aris R. <abdularisrahmanudin10@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#ifndef TRANSFERSERVER_H
#define TRANSFERSERVER_H

#include <QTcpServer>
#include <QObject>
#include <QSslCertificate>
#include <QSslKey>
#include <QQueue>

#include "receiver.h"
#include "model/devicelistmodel.h"

class TransferServer : public QObject
{
    Q_OBJECT

public:
    explicit TransferServer(DeviceListModel* devList, QObject *parent = nullptr);

    bool listen(const QHostAddress& addr = QHostAddress::Any);
    int activeDownloadCount() const;

Q_SIGNALS:
    void newReceiverAdded(Receiver* receiver);

private Q_SLOTS:
    void onNewConnection();
    void onReceiverFinished();

private:
    void startReceiver(QTcpSocket* socket);
    void rejectBusy(QTcpSocket* socket);
    void tryStartPending();
    bool isActiveReceiver(const Receiver* receiver) const;

    DeviceListModel* mDevList;
    QTcpServer* mServer;
    QVector<Receiver*> mReceivers;
    QQueue<QTcpSocket*> mPendingSockets;
    QSslCertificate mServerCert;
    QSslKey mServerKey;
    bool mTlsCredentialsReady{false};
};

#endif // TRANSFERSERVER_H
