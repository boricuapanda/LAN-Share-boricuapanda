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

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDir>
#include <QtDebug>
#include <QStringList>
#include <QSslSocket>
#include <QSslError>
#include <QUuid>
#include <QDataStream>
#include <QTimer>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QDateTime>

#include "settings.h"
#include "sender.h"
#include "log.h"
#include "tlshelper.h"
#include "transfer/transferjournal.h"

Sender::Sender(const Device& receiver, const QString& folderName, const QString& filePath, QObject* parent)
    : Transfer(nullptr, parent), mReceiverDev(receiver), mFilePath(filePath), mFolderName(folderName)
{
    mFileSize = -1;
    mBytesRemaining = -1;

    mFileBuffSize = Settings::instance()->getFileBufferSize();
    mFileBuff.resize(mFileBuffSize);

    mCancelled = false;
    mPaused = false;
    mPausedByReceiver = false;
    mIsHeaderSent = false;
    mWaitingForOffsetAck = false;
    mStartedTlsHandshake = false;
    mFinishing = false;
    mFinishPending = false;
    mSendScheduled = false;
    mReceiverProgressAcks = false;
    mReceiverFinishAck = false;
    mRequestedStreams = Settings::instance()->getParallelStreams();
    if (Settings::instance()->getTlsEnabled() && mRequestedStreams > 1) {
        AppLog::write("transfer",
                      tr("Parallel streams disabled while TLS is enabled; using single-stream transfer."));
        mRequestedStreams = 1;
    }
    mActiveStreams = 1;
    mStripedSocketIndex = 0;
    mNextStripedOffset = 0;
    mVerifyChecksum = Settings::instance()->getVerifyChecksum();
    mTransferId = QUuid::createUuid().toString(QUuid::WithoutBraces);
    mHash = mVerifyChecksum ? new QCryptographicHash(QCryptographicHash::Sha256) : nullptr;
    mOffsetAckTimer = new QTimer(this);
    mOffsetAckTimer->setSingleShot(true);
    connect(mOffsetAckTimer, &QTimer::timeout, this, &Sender::onOffsetAckTimeout);
    mRetryScheduled = false;

    mInfo->setTransferType(TransferType::Upload);
    mInfo->setPeer(receiver);
    mInfo->setTransferId(mTransferId);
    connect(mInfo, &TransferInfo::errorOcurred, this, [this]() {
        TransferJournal::instance()->remove(mTransferId);
    });
}

namespace {

void journalCheckpointUpload(const Sender* sender, TransferState state)
{
    if (!Settings::instance()->getJournalEnabled())
        return;

    const TransferInfo* info = sender->getTransferInfo();
    JournalEntry entry;
    entry.transferId = info->getTransferId();
    entry.type = TransferType::Upload;
    entry.filePath = info->getFilePath();
    entry.peerAddress = info->getPeer().displayAddress();
    entry.state = state;
    entry.dataSize = info->getDataSize();
    entry.bytesTransferred = info->getBytesTransferred();
    entry.fingerprint = TransferJournal::makeFingerprint(entry.transferId, entry.dataSize, entry.filePath);
    entry.updatedAtMs = QDateTime::currentMSecsSinceEpoch();
    TransferJournal::instance()->upsert(entry);
}

} // namespace

bool Sender::start()
{
    mInfo->setFilePath(mFilePath);
    mFile = new QFile(mFilePath, this);
    bool ok = mFile->open(QIODevice::ReadOnly);
    if (ok) {
        mFileSize = mFile->size();
        mInfo->setDataSize(mFileSize);
        mBytesRemaining = mFileSize;
        emit mInfo->fileOpened();
    }

    if (mFileSize > 0) {
        connectTransferSocket();
    }

    return ok && mSocket;
}

void Sender::connectTransferSocket()
{
    QHostAddress receiverAddress = mReceiverDev.getAddress();
    if (Settings::instance()->getTlsEnabled())
        setSocket(new QSslSocket(this));
    else
        setSocket(new QTcpSocket(this));

    connect(mSocket, &QTcpSocket::bytesWritten, this, &Sender::onBytesWritten);
    connect(mSocket, &QTcpSocket::connected, this, &Sender::onConnected);
    connect(mSocket, &QTcpSocket::disconnected, this, &Sender::onDisconnected);
    connect(mSocket, &QAbstractSocket::errorOccurred, this, &Sender::onSocketError);
    if (auto* sslSocket = qobject_cast<QSslSocket*>(mSocket)) {
        connect(sslSocket, &QSslSocket::encrypted, this, &Sender::onEncrypted);
        connect(sslSocket,
                QOverload<const QList<QSslError>&>::of(&QSslSocket::sslErrors),
                this,
                &Sender::onSslErrors);
    }

    mSocket->connectToHost(receiverAddress, Settings::instance()->getTransferPort(), QAbstractSocket::ReadWrite);
    mInfo->setState(TransferState::Waiting);
    journalCheckpointUpload(this, TransferState::Waiting);
    if (mInfo->getAttempt() == 0) {
        AppLog::transferEvent(mTransferId, QStringLiteral("start"), mReceiverDev.displayAddress(),
                              QStringLiteral("-"), QString::number(mInfo->getAttempt()), mFilePath);
    }
}

void Sender::scheduleRetry(int delayMs, int nextAttempt)
{
    if (mRetryScheduled || mCancelled)
        return;

    mRetryScheduled = true;
    AppLog::transferEvent(mTransferId, QStringLiteral("retry"),
                          mReceiverDev.displayAddress(),
                          QStringLiteral("peer_disconnected"),
                          QString::number(nextAttempt),
                          tr("Retrying in %1 ms").arg(delayMs));
    QTimer::singleShot(delayMs, this, [this, nextAttempt]() {
        mRetryScheduled = false;
        if (mCancelled || mInfo->getState() == TransferState::Cancelled
            || mInfo->getState() == TransferState::Finish) {
            return;
        }
        retryConnect(nextAttempt);
    });
}

void Sender::retryConnect(int attempt)
{
    mInfo->setAttempt(attempt);
    mIsHeaderSent = false;
    mWaitingForOffsetAck = false;
    mPausedByReceiver = false;
    mStartedTlsHandshake = false;
    mFinishing = false;
    mFinishPending = false;
    mSendScheduled = false;
    mReceiverProgressAcks = false;
    mReceiverFinishAck = false;
    mStripedSocketIndex = 0;
    mOffsetAckTimer->stop();
    clearReadBuffer();
    closeDataSockets();

    if (mSocket) {
        mSocket->disconnect(this);
        mSocket->deleteLater();
        mSocket = nullptr;
    }

    if (!mFile)
        mFile = new QFile(mFilePath, this);
    if (!mFile->isOpen() && !mFile->open(QIODevice::ReadOnly)) {
        mInfo->fail(TransferFailureReason::FileIoError, tr("Error reopening file for retry."));
        return;
    }
    if (!mFile->seek(0)) {
        mInfo->fail(TransferFailureReason::FileIoError, tr("Error seeking file for retry."));
        return;
    }
    if (mVerifyChecksum && mHash)
        mHash->reset();
    mFileSize = mFile->size();
    mInfo->setDataSize(mFileSize);
    mBytesRemaining = mFileSize;
    resetIdleTimer();

    connectTransferSocket();
}

void Sender::resume()
{
    if (mInfo->canResume()) {
        mInfo->setState(mInfo->getLastState());
        mPaused = false;
        sendData();
    }
}

void Sender::pause()
{
    if (mInfo->canPause()) {
        mInfo->setState(TransferState::Paused);
        mPaused = true;
        closeDataSockets();
    }
}

void Sender::cancel()
{
    if (!mInfo->canCancel())
        return;

    if (mInfo->getState() == TransferState::Queued || !mSocket) {
        mInfo->setState(TransferState::Cancelled);
        mInfo->setProgress(0);
        mCancelled = true;
        return;
    }

    if (mInfo->canCancel()) {
        writePacket(0, PacketType::Cancel, QByteArray());
        closeDataSockets();
        mInfo->setState(TransferState::Cancelled);
        mInfo->setProgress(0);
        mCancelled = true;
    }
}

void Sender::onConnected()
{
    auto* sslSocket = qobject_cast<QSslSocket*>(mSocket);
    if (Settings::instance()->getTlsEnabled() && sslSocket) {
        if (!QSslSocket::supportsSsl()) {
            mInfo->fail(TransferFailureReason::TlsError,
                        tr("TLS setup failed: OpenSSL 1.1 runtime is missing or could not be loaded."));
            mSocket->disconnectFromHost();
            return;
        }

        mStartedTlsHandshake = true;
        // Token auth is still enforced at packet level, so peer certificate verification
        // is disabled for now to keep rollout compatible with self-signed local certs.
        sslSocket->setPeerVerifyMode(QSslSocket::VerifyNone);
        sslSocket->startClientEncryption();
        return;
    }

    onEncrypted();
}

void Sender::onEncrypted()
{
    if (auto* sslSocket = qobject_cast<QSslSocket*>(mSocket)) {
        QString tlsError;
        const QString peerId = mReceiverDev.displayAddress();
        if (!TlsHelper::checkAndPinPeerCertificate(peerId, sslSocket->peerCertificate(), &tlsError)) {
            mInfo->fail(TransferFailureReason::TlsError, tlsError);
            mSocket->disconnectFromHost();
            return;
        }
    }

    mInfo->setState(TransferState::Transfering);
    sendHeader();
}

void Sender::onDisconnected()
{
    if (mInfo->getState() == TransferState::Failed || mInfo->getState() == TransferState::Cancelled ||
        mInfo->getState() == TransferState::Finish) {
        return;
    }
    if (mFinishing || mFinishPending || mBytesRemaining == 0) {
        if (mFinishing)
            completeUpload();
        return;
    }

    closeDataSockets();
    resetIdleTimer();
    journalCheckpointUpload(this, TransferState::Disconnected);

    const int retryMax = Settings::instance()->getTransferRetryMax();
    if (!mCancelled && !mPaused && mInfo->getAttempt() < retryMax) {
        mInfo->setState(TransferState::Waiting);
        const int nextAttempt = mInfo->getAttempt() + 1;
        const int delayMs = Settings::instance()->getTransferRetryBaseMs() * (1 << (nextAttempt - 1));
        scheduleRetry(delayMs, nextAttempt);
        return;
    }

    mInfo->fail(TransferFailureReason::PeerDisconnected, tr("Receiver disconnected"));
}

void Sender::onSocketError(QAbstractSocket::SocketError error)
{
    if (mInfo->getState() == TransferState::Failed || mInfo->getState() == TransferState::Cancelled ||
        mInfo->getState() == TransferState::Finish) {
        return;
    }

    if (mFinishing && mBytesRemaining == 0)
        return;

    if (error == QAbstractSocket::SslHandshakeFailedError || mStartedTlsHandshake) {
        mInfo->fail(TransferFailureReason::TlsError,
                    tr("TLS connection failed: %1").arg(mSocket->errorString()));
    }
}

void Sender::onSslErrors(const QList<QSslError>& errors)
{
    Q_UNUSED(errors);
    if (auto* sslSocket = qobject_cast<QSslSocket*>(mSocket))
        sslSocket->ignoreSslErrors();
}

void Sender::onBytesWritten(qint64 bytes)
{
    Q_UNUSED(bytes);

    if (!mSocket->bytesToWrite()) {
        if (mFinishPending)
            finish();
        else
            scheduleSendData();
    }
}

void Sender::finish()
{
    if (mInfo->getState() == TransferState::Failed || mInfo->getState() == TransferState::Cancelled ||
        mInfo->getState() == TransferState::Finish || mInfo->getState() == TransferState::Disconnected) {
        return;
    }
    if (!mSocket || mSocket->state() != QAbstractSocket::ConnectedState)
        return;
    if (mSocket->bytesToWrite() > 0) {
        mFinishPending = true;
        mSocket->flush();
        return;
    }

    QByteArray finishData;
    if (mVerifyChecksum && mHash) {
        QJsonObject obj({{"sha256", QString::fromLatin1(mHash->result().toHex())}});
        finishData = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    }

    writePacket(finishData.size(), PacketType::Finish, finishData);
    mFinishPending = false;
    mFinishing = true;
    if (mSocket)
        mSocket->flush();

    if (!mReceiverFinishAck)
        completeUpload();
}

void Sender::completeUpload()
{
    if (mInfo->getState() == TransferState::Failed || mInfo->getState() == TransferState::Cancelled ||
        mInfo->getState() == TransferState::Finish || mInfo->getState() == TransferState::Disconnected) {
        return;
    }

    mFinishing = false;
    mFinishPending = false;
    mSendScheduled = false;
    if (mFileSize > 0) {
        mInfo->setBytesTransferred(mFileSize);
        mInfo->setProgress(100);
    }
    mInfo->setState(TransferState::Finish);
    TransferJournal::instance()->remove(mTransferId);
    AppLog::transferEvent(mTransferId, QStringLiteral("finish"), mReceiverDev.displayAddress(),
                          QStringLiteral("-"), QString::number(mInfo->getAttempt()), mFilePath);
    if (mFile)
        mFile->close();
    emit mInfo->done();
}

void Sender::sendData()
{
    mSendScheduled = false;

    if (!mSocket || mSocket->state() != QAbstractSocket::ConnectedState)
        return;
    if (mInfo->getState() == TransferState::Failed || mInfo->getState() == TransferState::Cancelled ||
        mInfo->getState() == TransferState::Finish || mInfo->getState() == TransferState::Disconnected) {
        return;
    }
    if (!mBytesRemaining || mCancelled || mPausedByReceiver || mPaused || mWaitingForOffsetAck)
        return;

    if (mActiveStreams > 1) {
        sendStripedData();
        return;
    }

    if (mBytesRemaining < mFileBuffSize) {
        mFileBuff.resize(mBytesRemaining);
        mFileBuffSize = mFileBuff.size();
    }

    qint64 bytesRead = mFile->read(mFileBuff.data(), mFileBuffSize);
    if (bytesRead == -1) {
        mInfo->fail(TransferFailureReason::FileIoError, tr("Error while reading file."));
        return;
    }

    if (mVerifyChecksum && mHash)
        mHash->addData(mFileBuff.constData(), bytesRead);

    mBytesRemaining -= bytesRead;
    if (mBytesRemaining < 0)
        mBytesRemaining = 0;

    if (!mReceiverProgressAcks) {
        mInfo->setProgress( (int) ((mFileSize-mBytesRemaining) * 100 / mFileSize) );
        mInfo->setBytesTransferred(mFileSize - mBytesRemaining);
    }

    writePacket(static_cast<qint32>(bytesRead), PacketType::Data, mFileBuff.left(bytesRead));

    if (!mBytesRemaining) {
        mFinishPending = true;
        if (mSocket && mSocket->bytesToWrite() > 0)
            mSocket->flush();
        else
            finish();
    }
}

void Sender::scheduleSendData(int delayMs)
{
    if (mSendScheduled || mCancelled || mPaused || mPausedByReceiver || mWaitingForOffsetAck)
        return;

    mSendScheduled = true;
    QTimer::singleShot(delayMs, this, &Sender::sendData);
}

bool Sender::setupDataSockets()
{
    if (mActiveStreams <= 1 || !mDataSockets.isEmpty())
        return true;

    if (mNextStripedOffset > 0) {
        mActiveStreams = 1;
        return false;
    }

    for (int i = 0; i < mActiveStreams; ++i) {
        QTcpSocket* socket = new QTcpSocket(this);
        socket->connectToHost(mReceiverDev.getAddress(), Settings::instance()->getTransferPort());
        if (!socket->waitForConnected(3000)) {
            AppLog::write("transfer", tr("Parallel stream socket connect failed, fallback to single stream."));
            socket->deleteLater();
            closeDataSockets();
            mActiveStreams = 1;
            return false;
        }

        QJsonObject attachObj({
            {"stream_attach", true},
            {"transfer_id", mTransferId},
            {"stream_index", i}
        });
        const QByteArray attachData = QJsonDocument(attachObj).toJson(QJsonDocument::Compact);
        qint32 packetSize = attachData.size();
        const char type = static_cast<char>(PacketType::Header);
        socket->write(reinterpret_cast<const char*>(&packetSize), sizeof(packetSize));
        socket->write(&type, sizeof(type));
        socket->write(attachData);
        if (!socket->waitForBytesWritten(3000)) {
            AppLog::write("transfer", tr("Parallel stream attach write failed, fallback to single stream."));
            socket->deleteLater();
            closeDataSockets();
            mActiveStreams = 1;
            return false;
        }

        QByteArray ackBuffer;
        qint32 ackPacketSize = -1;
        QElapsedTimer ackTimer;
        ackTimer.start();
        while (true) {
            if (!socket->bytesAvailable())
                socket->waitForReadyRead(10);
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            ackBuffer.append(socket->readAll());
            if (ackPacketSize < 0 && ackBuffer.size() >= static_cast<int>(sizeof(qint32)))
                memcpy(&ackPacketSize, ackBuffer.constData(), sizeof(ackPacketSize));
            if (ackPacketSize >= 0 &&
                ackBuffer.size() >= static_cast<int>(sizeof(qint32) + sizeof(char) + ackPacketSize)) {
                const PacketType ackType =
                    static_cast<PacketType>(ackBuffer.at(sizeof(qint32)));
                if (ackType != PacketType::OffsetAck) {
                    AppLog::write("transfer", tr("Parallel stream attach ack invalid, fallback to single stream."));
                    socket->deleteLater();
                    closeDataSockets();
                    mActiveStreams = 1;
                    return false;
                }
                break;
            }
            if (ackTimer.elapsed() > 3000) {
                AppLog::write("transfer", tr("Parallel stream attach ack timeout, fallback to single stream."));
                socket->deleteLater();
                closeDataSockets();
                mActiveStreams = 1;
                return false;
            }
        }
        mDataSockets.push_back(socket);
    }

    return true;
}

void Sender::closeDataSockets()
{
    for (QTcpSocket* socket : mDataSockets) {
        if (!socket)
            continue;
        socket->disconnectFromHost();
        socket->deleteLater();
    }
    mDataSockets.clear();
}

bool Sender::writeStripedChunk(QTcpSocket* socket, qint64 offset, const QByteArray& chunk)
{
    if (!socket)
        return false;

    QByteArray payload;
    QDataStream out(&payload, QIODevice::WriteOnly);
    out.setByteOrder(QDataStream::LittleEndian);
    out << offset;
    out << static_cast<qint32>(chunk.size());
    payload.append(chunk);

    const qint32 packetSize = payload.size();
    const char type = static_cast<char>(PacketType::StripedData);
    if (socket->write(reinterpret_cast<const char*>(&packetSize), sizeof(packetSize)) < 0)
        return false;
    if (socket->write(&type, sizeof(type)) < 0)
        return false;
    return socket->write(payload) >= 0;
}

void Sender::sendStripedData()
{
    mSendScheduled = false;

    if (!setupDataSockets()) {
        scheduleSendData();
        return;
    }

    if (mBytesRemaining <= 0) {
        finish();
        return;
    }

    if (mCancelled || mPaused || mPausedByReceiver)
        return;

    QTcpSocket* socket = mDataSockets.at(mStripedSocketIndex % mDataSockets.size());
    const qint64 maxPendingBytes = qMax<qint64>(mFileBuffSize * 8, 1024 * 1024);
    if (socket->bytesToWrite() > maxPendingBytes) {
        mSendScheduled = true;
        QTimer::singleShot(10, this, &Sender::sendStripedData);
        return;
    }

    const int chunkSize = qMin<qint64>(mFileBuffSize, mBytesRemaining);
    QByteArray chunk = mFile->read(chunkSize);
    if (chunk.isEmpty()) {
        mInfo->fail(TransferFailureReason::FileIoError, tr("Error while reading file."));
        return;
    }

    if (mVerifyChecksum && mHash)
        mHash->addData(chunk);

    if (!writeStripedChunk(socket, mNextStripedOffset, chunk)) {
        AppLog::write("transfer", tr("Parallel stream write failed, cancelling transfer."));
        writePacket(0, PacketType::Cancel, QByteArray());
        if (mSocket) {
            mSocket->flush();
            if (mSocket->state() == QAbstractSocket::ConnectedState)
                mSocket->waitForBytesWritten(1000);
            mSocket->disconnectFromHost();
        }
        closeDataSockets();
        mInfo->fail(TransferFailureReason::ParallelStreamError, tr("Parallel stream write failed."));
        return;
    }

    mNextStripedOffset += chunk.size();
    mBytesRemaining -= chunk.size();
    if (!mReceiverProgressAcks) {
        mInfo->setProgress((int)((mFileSize - mBytesRemaining) * 100 / mFileSize));
        mInfo->setBytesTransferred(mFileSize - mBytesRemaining);
    }
    ++mStripedSocketIndex;

    if (mBytesRemaining == 0) {
        finish();
        return;
    }

    mSendScheduled = true;
    QTimer::singleShot(1, this, &Sender::sendStripedData);
}

void Sender::sendHeader()
{
    QString fName = QDir(mFile->fileName()).dirName();
    const bool authEnabled = Settings::instance()->getAuthEnabled();
    const QString authToken = Settings::instance()->getAuthToken();
    QString authHash;
    if (authEnabled && !authToken.isEmpty()) {
        authHash = QString::fromLatin1(
            QCryptographicHash::hash(authToken.toUtf8(), QCryptographicHash::Sha256).toHex());
    }

    QJsonObject obj( QJsonObject::fromVariantMap({
                                    {"name", fName},
                                    {"folder", mFolderName },
                                    {"size", mFileSize},
                                    {"verify", mVerifyChecksum},
                                    {"protocol", TransferProtocol::CurrentVersion},
                                    {"magic", static_cast<qint64>(TransferProtocol::Magic)},
                                    {"parallel_supported", mRequestedStreams > 1},
                                    {"streams", mRequestedStreams},
                                    {"transfer_id", mTransferId},
                                    {"auth", authEnabled},
                                    {"auth_hash", authHash}
                                }));

    QByteArray headerData( QJsonDocument(obj).toJson() );

    writePacket(headerData.size(), PacketType::Header, headerData);
    mIsHeaderSent = true;
    beginOffsetAckWait();
}

void Sender::beginOffsetAckWait()
{
    mWaitingForOffsetAck = true;
    mOffsetAckTimer->start(Settings::instance()->getTransferOffsetAckTimeoutMs());
}

void Sender::onOffsetAckTimeout()
{
    if (!mWaitingForOffsetAck)
        return;

    AppLog::write("transfer",
                  QString("OffsetAck timeout from %1; assuming legacy receiver and continuing from offset 0")
                      .arg(mReceiverDev.displayAddress()));
    applyOffsetAck(0, 1, false);
}

void Sender::applyOffsetAck(qint64 offset, int acceptedStreams, bool peerSupportsParallel)
{
    mOffsetAckTimer->stop();
    if (offset > 0) {
        peerSupportsParallel = false;
        acceptedStreams = 1;
        closeDataSockets();
    }
    mActiveStreams = (peerSupportsParallel ? acceptedStreams : 1);
    if (offset > 0 && mActiveStreams > 1) {
        AppLog::write("transfer", QString("Parallel resume disabled: offset=%1, fallback to single stream").arg(offset));
        mActiveStreams = 1;
    }
    mStripedSocketIndex = 0;
    if (mActiveStreams != mRequestedStreams) {
        AppLog::write("transfer",
                      QString("Parallel stream fallback: requested=%1 accepted=%2")
                          .arg(mRequestedStreams)
                          .arg(mActiveStreams));
    }

    if (offset < 0 || offset > mFileSize) {
        mWaitingForOffsetAck = false;
        writePacket(0, PacketType::Cancel, QByteArray());
        mSocket->disconnectFromHost();
        mInfo->fail(TransferFailureReason::InvalidResumeOffset, tr("Invalid resume offset from receiver."));
        return;
    }

    if (offset > 0) {
        if (!mFile->seek(offset)) {
            mWaitingForOffsetAck = false;
            writePacket(0, PacketType::Cancel, QByteArray());
            mSocket->disconnectFromHost();
            mInfo->fail(TransferFailureReason::InvalidResumeOffset, tr("Failed to seek file for resume."));
            return;
        }
        hashFilePrefix(offset);
    } else if (mVerifyChecksum && mHash) {
        mHash->reset();
    }

    mBytesRemaining = mFileSize - offset;
    mNextStripedOffset = offset;
    if (mFileSize > 0)
        mInfo->setProgress((int)((mFileSize - mBytesRemaining) * 100 / mFileSize));

    mWaitingForOffsetAck = false;
    AppLog::write("transfer", QString("Upload resumed at offset %1: %2").arg(offset).arg(mFilePath));
    scheduleSendData();
}

void Sender::hashFilePrefix(qint64 length)
{
    if (!mVerifyChecksum || !mHash || length <= 0)
        return;

    mHash->reset();
    const qint64 savedPos = mFile->pos();
    mFile->seek(0);

    QByteArray buffer;
    buffer.resize(mFileBuffSize);
    qint64 remaining = length;
    while (remaining > 0) {
        const qint64 toRead = qMin<qint64>(mFileBuffSize, remaining);
        const qint64 bytesRead = mFile->read(buffer.data(), toRead);
        if (bytesRead <= 0)
            break;
        mHash->addData(buffer.constData(), bytesRead);
        remaining -= bytesRead;
    }

    mFile->seek(savedPos);
}

void Sender::processOffsetAckPacket(QByteArray& data)
{
    if (mInfo->getState() == TransferState::Failed || mInfo->getState() == TransferState::Cancelled
        || mInfo->getState() == TransferState::Finish) {
        return;
    }

    QJsonObject obj = QJsonDocument::fromJson(data).object();
    if (obj.value(QStringLiteral("progress_ack")).toBool(false)) {
        const qint64 bytesReceived = qBound<qint64>(0, obj.value(QStringLiteral("bytes_received")).toVariant().toLongLong(), mFileSize);
        mInfo->setBytesTransferred(bytesReceived);
        if (mFileSize > 0)
            mInfo->setProgress(static_cast<int>(bytesReceived * 100 / mFileSize));
        return;
    }

    if (obj.value(QStringLiteral("finish_ack")).toBool(false)) {
        const qint64 bytesReceived = qBound<qint64>(0, obj.value(QStringLiteral("bytes_received")).toVariant().toLongLong(), mFileSize);
        mInfo->setBytesTransferred(bytesReceived);
        if (mFileSize > 0)
            mInfo->setProgress(static_cast<int>(bytesReceived * 100 / mFileSize));
        completeUpload();
        return;
    }

    if (!mWaitingForOffsetAck)
        return;

    if (obj.value(QStringLiteral("busy")).toBool(false)) {
        const int retryAfterMs = obj.value(QStringLiteral("retry_after_ms")).toInt(1000);
        mWaitingForOffsetAck = false;
        mOffsetAckTimer->stop();

        const int retryMax = Settings::instance()->getTransferRetryMax();
        if (!mCancelled && mInfo->getAttempt() < retryMax) {
            const int nextAttempt = mInfo->getAttempt() + 1;
            if (mSocket)
                mSocket->disconnectFromHost();
            scheduleRetry(retryAfterMs, nextAttempt);
            return;
        }

        mInfo->fail(TransferFailureReason::AdmissionBusy,
                    tr("Receiver busy. Retry after %1 ms.").arg(retryAfterMs));
        if (mSocket)
            mSocket->disconnectFromHost();
        return;
    }

    const qint64 offset = obj.value("offset").toVariant().toLongLong();
    const int peerProtocol = obj.value(QStringLiteral("protocol")).toInt(1);
    mReceiverProgressAcks = obj.value(QStringLiteral("progress_ack_ready")).toBool(false)
                            && peerProtocol >= TransferProtocol::CurrentVersion;
    mReceiverFinishAck = obj.value(QStringLiteral("finish_ack_ready")).toBool(false)
                         && peerProtocol >= TransferProtocol::CurrentVersion;
    const bool peerSupportsParallel = obj.value("parallel_supported").toBool(false)
                                      && obj.value(QStringLiteral("parallel_ready")).toBool(false)
                                      && peerProtocol >= TransferProtocol::CurrentVersion;
    const int acceptedStreams = qMax(1, obj.value("accepted_streams").toInt(1));
    applyOffsetAck(offset, acceptedStreams, peerSupportsParallel);
}

void Sender::processCancelPacket(QByteArray& data)
{
    Q_UNUSED(data);

    mInfo->setState(TransferState::Cancelled);
    mInfo->setProgress(0);
    closeDataSockets();
    TransferJournal::instance()->remove(mTransferId);
    mSocket->disconnectFromHost();
    mCancelled = true;
}

void Sender::processPausePacket(QByteArray& data)
{
    Q_UNUSED(data);

    mPausedByReceiver = true;
}

void Sender::processResumePacket(QByteArray& data)
{
    Q_UNUSED(data);
    
    mPausedByReceiver = false;
    if (mIsHeaderSent)
        scheduleSendData();
    else
        sendHeader();
}
