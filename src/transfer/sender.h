/*
    LANShare - LAN file transfer.
    Copyright (C) 2016 Abdul Aris R. <abdularisrahmanudin10@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef SENDER_H
#define SENDER_H

#include "transfer.h"
#include "model/device.h"
#include <QVector>

class QCryptographicHash;
class QSslSocket;
class QSslError;
class QTimer;

class Sender : public Transfer
{
public:
    Sender(const Device& receiver, const QString& folderName, const QString& filePath, QObject* parent = nullptr);

    bool start();

    Device getReceiver() const { return mReceiverDev; }

    void resume() override;
    void pause() override;
    void cancel() override;

private Q_SLOTS:
    void onBytesWritten(qint64 bytes);
    void onConnected();
    void onEncrypted();
    void onDisconnected();
    void onSocketError(QAbstractSocket::SocketError error);
    void onSslErrors(const QList<QSslError>& errors);
    void onOffsetAckTimeout();

private:
    void finish();
    void sendData();
    void scheduleSendData(int delayMs = 1);
    void sendStripedData();
    void sendHeader();
    bool setupDataSockets();
    void closeDataSockets();
    bool writeStripedChunk(QTcpSocket* socket, qint64 offset, const QByteArray& chunk);

    void processCancelPacket(QByteArray& data) override;
    void processPausePacket(QByteArray& data) override;
    void processResumePacket(QByteArray& data) override;
    void processOffsetAckPacket(QByteArray& data) override;

    void hashFilePrefix(qint64 length);
    void beginOffsetAckWait();
    void applyOffsetAck(qint64 offset, int acceptedStreams, bool peerSupportsParallel);
    void scheduleRetry(int delayMs, int nextAttempt);
    void retryConnect(int attempt);
    void connectTransferSocket();

    Device mReceiverDev;
    QString mFilePath;
    QString mFolderName;
    qint64 mFileSize;
    qint64 mBytesRemaining;

    QByteArray mFileBuff;
    qint32 mFileBuffSize;

    bool mCancelled;
    bool mPaused;
    bool mPausedByReceiver;
    bool mIsHeaderSent;
    bool mWaitingForOffsetAck;
    bool mStartedTlsHandshake;
    bool mFinishing;
    bool mFinishPending;
    bool mSendScheduled;
    int mRequestedStreams;
    int mActiveStreams;
    int mStripedSocketIndex;
    qint64 mNextStripedOffset;
    QString mTransferId;
    QVector<QTcpSocket*> mDataSockets;
    bool mVerifyChecksum;
    QCryptographicHash* mHash;
    QTimer* mOffsetAckTimer;
    bool mRetryScheduled;
};

#endif // SENDER_H
