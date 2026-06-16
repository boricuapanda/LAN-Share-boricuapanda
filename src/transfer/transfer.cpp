/*
    LANShare - LAN file transfer.
    Copyright (C) 2016 Abdul Aris R. <abdularisrahmanudin10@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include "transfer.h"

#include "settings.h"
#include "log.h"

Transfer::Transfer(QTcpSocket* socket, QObject* parent)
    : QObject(parent), mFile(nullptr), mSocket(nullptr),
      mPacketSize(-1)
{
    mInfo = new TransferInfo(this, this);
    mIdleTimer = new QTimer(this);
    mIdleTimer->setSingleShot(true);
    connect(mIdleTimer, &QTimer::timeout, this, &Transfer::onIdleTimeout);
    setSocket(socket);
    mHeaderSize = sizeof(PacketType) + sizeof(mPacketSize);
}

void Transfer::resume()
{
}

void Transfer::pause()
{
}

void Transfer::cancel()
{
}

bool Transfer::isTerminalState() const
{
    const TransferState state = mInfo->getState();
    return state == TransferState::Cancelled
           || state == TransferState::Finish
           || state == TransferState::Failed
           || state == TransferState::Disconnected;
}

bool Transfer::isValidPacketType(PacketType type) const
{
    switch (type) {
    case PacketType::Header:
    case PacketType::Data:
    case PacketType::Finish:
    case PacketType::Cancel:
    case PacketType::Pause:
    case PacketType::Resume:
    case PacketType::OffsetAck:
    case PacketType::StreamAttach:
    case PacketType::StripedData:
        return true;
    }
    return false;
}

void Transfer::failTransfer(TransferFailureReason reason, const QString& message)
{
    if (isTerminalState())
        return;

    mInfo->fail(reason, message);
    clearReadBuffer();
    if (mSocket && mSocket->state() == QAbstractSocket::ConnectedState)
        mSocket->disconnectFromHost();
}

void Transfer::touchActivity()
{
    if (!mIdleTimer)
        return;

    const int timeoutMs = Settings::instance()->getTransferIdleTimeoutMs();
    if (timeoutMs <= 0) {
        mIdleTimer->stop();
        return;
    }

    mIdleTimer->start(timeoutMs);
}

void Transfer::resetIdleTimer()
{
    if (mIdleTimer)
        mIdleTimer->stop();
}

void Transfer::onIdleTimeout()
{
    if (isTerminalState())
        return;

    failTransfer(TransferFailureReason::Timeout,
                 tr("Transfer timed out due to inactivity."));
}

bool Transfer::consumeNextPacket()
{
    if (mPacketSize < 0) {
        if (mBuff.size() < static_cast<int>(sizeof(mPacketSize)))
            return false;

        memcpy(&mPacketSize, mBuff.constData(), sizeof(mPacketSize));
        mBuff.remove(0, sizeof(mPacketSize));

        const qint32 maxPacketSize = Settings::instance()->getMaxPacketSize();
        if (mPacketSize < 0 || mPacketSize > maxPacketSize) {
            failTransfer(TransferFailureReason::PacketOversize,
                         tr("Rejected oversized or invalid packet (size %1).").arg(mPacketSize));
            return false;
        }
    }

    if (mBuff.size() < mPacketSize + 1)
        return false;

    const PacketType type = static_cast<PacketType>(mBuff.at(0));
    if (!isValidPacketType(type)) {
        failTransfer(TransferFailureReason::InvalidPacketType,
                     tr("Rejected invalid packet type."));
        return false;
    }

    QByteArray data = mBuff.mid(1, mPacketSize);
    mBuff.remove(0, mPacketSize + 1);
    mPacketSize = -1;

    if (!isTerminalState()) {
        processPacket(data, type);
        touchActivity();
    }
    return true;
}

void Transfer::onReadyRead()
{
    if (mInfo->getState() == TransferState::Cancelled || isTerminalState())
        return;

    mBuff.append(mSocket->readAll());
    touchActivity();

    while (mBuff.size() >= mHeaderSize) {
        if (!consumeNextPacket())
            break;
        if (isTerminalState())
            break;
    }
}

void Transfer::writePacket(qint32 packetDataSize, PacketType type, const QByteArray &data)
{
    if (!mSocket || mSocket->state() != QAbstractSocket::ConnectedState)
        return;

    mSocket->write(reinterpret_cast<const char*>(&packetDataSize), sizeof(packetDataSize));
    mSocket->write(reinterpret_cast<const char*>(&type), sizeof(type));
    mSocket->write(data);
    touchActivity();
}

void Transfer::processPacket(QByteArray &data, PacketType type)
{
    switch (type) {
    case PacketType::Header : processHeaderPacket(data); break;
    case PacketType::Data : processDataPacket(data); break;
    case PacketType::Finish : processFinishPacket(data); break;
    case PacketType::Cancel : processCancelPacket(data); break;
    case PacketType::Pause : processPausePacket(data); break;
    case PacketType::Resume : processResumePacket(data); break;
    case PacketType::OffsetAck : processOffsetAckPacket(data); break;
    case PacketType::StreamAttach : processHeaderPacket(data); break;
    case PacketType::StripedData : processDataPacket(data); break;
    }
}

void Transfer::processHeaderPacket(QByteArray& data)
{
    Q_UNUSED(data);
}

void Transfer::processDataPacket(QByteArray& data)
{
    Q_UNUSED(data);
}

void Transfer::processFinishPacket(QByteArray& data)
{
    Q_UNUSED(data);
}

void Transfer::processCancelPacket(QByteArray& data)
{
    Q_UNUSED(data);
}

void Transfer::processPausePacket(QByteArray& data)
{
    Q_UNUSED(data);
}

void Transfer::processResumePacket(QByteArray& data)
{
    Q_UNUSED(data);
}

void Transfer::processOffsetAckPacket(QByteArray& data)
{
    Q_UNUSED(data);
}

void Transfer::clearReadBuffer()
{
    mBuff.clear();
    mPacketSize = -1;
}

void Transfer::setSocket(QTcpSocket *socket)
{
    if (socket) {
        mSocket = socket;
        connect(mSocket, &QTcpSocket::readyRead, this, &Transfer::onReadyRead);
        touchActivity();
    }
}
