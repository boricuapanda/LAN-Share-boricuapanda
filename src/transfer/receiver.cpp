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
#include <QJsonObject>
#include <QJsonDocument>
#include <QDir>
#include <QFileInfo>
#include <QSslSocket>
#include <QSslError>
#include <QStringList>
#include <QDataStream>
#include <QMutex>
#include <QMutexLocker>
#include <QDateTime>

#include "util.h"
#include "receiver.h"
#include "settings.h"
#include "log.h"
#include "transfer/transferjournal.h"

namespace {
QHash<QString, Receiver*> gTransferRegistry;
QMutex gTransferRegistryMutex;
}

Receiver::Receiver(const Device& sender, QTcpSocket* socket, QObject* parent)
    : Transfer(socket, parent), mSenderDev(sender), mFileSize(0), mBytesRead(0)
{
    mVerifyChecksum = Settings::instance()->getVerifyChecksum();
    mResumePartialDownloads = Settings::instance()->getResumePartialDownloads();
    mAcceptedStreams = 1;
    mRegisteredTransferId = false;
    mAttachProxy = false;
    mFinishPending = false;
    mHash = mVerifyChecksum ? new QCryptographicHash(QCryptographicHash::Sha256) : nullptr;

    mInfo->setState(TransferState::Waiting);
    connect(mSocket, &QTcpSocket::disconnected, this, &Receiver::onDisconnected);
    connect(mSocket, &QAbstractSocket::errorOccurred, this, &Receiver::onSocketError);
    if (auto* sslSocket = qobject_cast<QSslSocket*>(mSocket)) {
        connect(sslSocket,
                QOverload<const QList<QSslError>&>::of(&QSslSocket::sslErrors),
                this,
                &Receiver::onSslErrors);
    }

    mInfo->setTransferType(TransferType::Download);
    mInfo->setPeer(sender);
}

namespace {

void journalCheckpointDownload(Receiver* receiver, TransferState state)
{
    if (!Settings::instance()->getJournalEnabled())
        return;

    const TransferInfo* info = receiver->getTransferInfo();
    JournalEntry entry;
    entry.transferId = receiver->getTransferId();
    entry.type = TransferType::Download;
    entry.filePath = receiver->getFinalFilePath();
    entry.partPath = receiver->getPartFilePath();
    entry.peerAddress = info->getPeer().getAddress().toString();
    entry.state = state;
    entry.dataSize = info->getDataSize();
    entry.bytesTransferred = info->getBytesTransferred();
    entry.fingerprint = TransferJournal::makeFingerprint(entry.transferId, entry.dataSize, entry.filePath);
    entry.updatedAtMs = QDateTime::currentMSecsSinceEpoch();
    TransferJournal::instance()->upsert(entry);
}

} // namespace

void Receiver::resume()
{
    if (mInfo->canResume()) {
        mInfo->setState(mInfo->getLastState());
        writePacket(0, PacketType::Resume, QByteArray());
    }
}

void Receiver::pause()
{
    if (mInfo->canPause()) {
        mInfo->setState(TransferState::Paused);
        writePacket(0, PacketType::Pause, QByteArray());
    }
}

void Receiver::cancel()
{
    if (mInfo->canCancel()) {
        mInfo->setState(TransferState::Cancelled);
        mInfo->setProgress(0);
        clearReadBuffer();
        writePacket(0, PacketType::Cancel, QByteArray());
        closeDataSockets();
        removePartFile();
        TransferJournal::instance()->remove(mTransferId);
        if (mFile) {
            mFile->close();
            mFile->remove();
        }
    }
}

void Receiver::onDisconnected()
{
    if (mRegisteredTransferId) {
        QMutexLocker lock(&gTransferRegistryMutex);
        gTransferRegistry.remove(mTransferId);
        mRegisteredTransferId = false;
    }
    if (mInfo->getState() == TransferState::Failed || mInfo->getState() == TransferState::Cancelled ||
        mInfo->getState() == TransferState::Finish || mInfo->getState() == TransferState::Disconnected) {
        return;
    }

    mInfo->fail(TransferFailureReason::PeerDisconnected, tr("Sender disconnected"));
}

void Receiver::onSocketError(QAbstractSocket::SocketError error)
{
    if (mInfo->getState() == TransferState::Failed || mInfo->getState() == TransferState::Cancelled ||
        mInfo->getState() == TransferState::Finish || mInfo->getState() == TransferState::Disconnected) {
        return;
    }

    if (error == QAbstractSocket::SslHandshakeFailedError) {
        mInfo->fail(TransferFailureReason::TlsError,
                    tr("TLS connection failed: %1").arg(mSocket->errorString()));
    }
}

void Receiver::onSslErrors(const QList<QSslError>& errors)
{
    Q_UNUSED(errors);
    // Self-signed local certs are validated via TOFU pinning on the sender side;
    // the receiver accepts the encrypted channel like QSslSocket::VerifyNone.
    if (auto* sslSocket = qobject_cast<QSslSocket*>(mSocket))
        sslSocket->ignoreSslErrors();
}

void Receiver::sendOffsetAck(qint64 offset, int acceptedStreams)
{
    QJsonObject obj({{"offset", offset},
                     {"parallel_supported", true},
                     {"accepted_streams", acceptedStreams}});
    const QByteArray data = QJsonDocument(obj).toJson(QJsonDocument::Compact);
    writePacket(data.size(), PacketType::OffsetAck, data);
}

void Receiver::hashExistingPartFile()
{
    if (!mVerifyChecksum || !mHash || !mFile || mBytesRead <= 0)
        return;

    mHash->reset();
    const qint64 savedPos = mFile->pos();
    mFile->seek(0);

    QByteArray buffer;
    buffer.resize(Settings::instance()->getFileBufferSize());
    qint64 remaining = mBytesRead;
    while (remaining > 0) {
        const qint64 toRead = qMin<qint64>(buffer.size(), remaining);
        const qint64 bytesRead = mFile->read(buffer.data(), toRead);
        if (bytesRead <= 0)
            break;
        mHash->addData(buffer.constData(), bytesRead);
        remaining -= bytesRead;
    }

    mFile->seek(savedPos);
}

void Receiver::removePartFile()
{
    if (!mPartFilePath.isEmpty())
        QFile::remove(mPartFilePath);
}

void Receiver::finalizeDownload()
{
    mFile->close();
    closeDataSockets();
    if (mRegisteredTransferId) {
        QMutexLocker lock(&gTransferRegistryMutex);
        gTransferRegistry.remove(mTransferId);
        mRegisteredTransferId = false;
    }

    if (mResumePartialDownloads && !mPartFilePath.isEmpty()) {
        if (QFile::exists(mFinalFilePath))
            QFile::remove(mFinalFilePath);
        if (!QFile::rename(mPartFilePath, mFinalFilePath)) {
            mInfo->fail(TransferFailureReason::FileIoError, tr("Failed to finalize download file."));
            mSocket->disconnectFromHost();
            return;
        }
        mPartFilePath.clear();
    }

    mInfo->setState(TransferState::Finish);
    TransferJournal::instance()->remove(mTransferId);
    AppLog::transferEvent(mTransferId, QStringLiteral("finish"), mSenderDev.getAddress().toString(),
                          QStringLiteral("-"), QStringLiteral("0"), mFinalFilePath);
    mSocket->disconnectFromHost();
    emit mInfo->done();
}

void Receiver::processHeaderPacket(QByteArray& data)
{
    QJsonObject obj = QJsonDocument::fromJson(data).object();
    if (obj.value("stream_attach").toBool(false)) {
        mAttachProxy = true;
        const QString transferId = obj.value("transfer_id").toString();
        Receiver* target = nullptr;
        {
            QMutexLocker lock(&gTransferRegistryMutex);
            target = gTransferRegistry.value(transferId, nullptr);
        }
        if (!target || !mSocket) {
            if (mSocket)
                mSocket->disconnectFromHost();
            return;
        }
        disconnect(mSocket, nullptr, this, nullptr);
        mSocket->setParent(target);
        target->adoptDataSocket(mSocket);
        mSocket = nullptr;
        return;
    }

    mFileSize = obj.value("size").toVariant().value<qint64>();
    mInfo->setDataSize(mFileSize);
    const bool senderAuthEnabled = obj.value("auth").toBool(false);
    const QString senderAuthHash = obj.value("auth_hash").toString();
    const bool localAuthEnabled = Settings::instance()->getAuthEnabled();
    const QString localAuthToken = Settings::instance()->getAuthToken();
    if (localAuthEnabled) {
        const QString localAuthHash = QString::fromLatin1(
            QCryptographicHash::hash(localAuthToken.toUtf8(), QCryptographicHash::Sha256).toHex());
        if (!senderAuthEnabled || senderAuthHash.isEmpty() || senderAuthHash != localAuthHash) {
            AppLog::write("transfer", QString("Authentication failed from %1")
                                       .arg(mSenderDev.getAddress().toString()));
            writePacket(0, PacketType::Cancel, QByteArray());
            mSocket->disconnectFromHost();
            mInfo->fail(TransferFailureReason::AuthFailed, tr("Authentication failed. Token mismatch."));
            return;
        }
    }

    const bool senderVerifyAdvertised = obj.contains("verify");
    const bool senderVerify = obj.value("verify").toBool(false);
    const int requestedStreams = qMax(1, obj.value("streams").toInt(1));
    const bool senderParallelSupported = obj.value("parallel_supported").toBool(false);
    mTransferId = obj.value("transfer_id").toString();
    mInfo->setTransferId(mTransferId);
    if (Settings::instance()->getVerifyChecksum() && senderVerifyAdvertised && !senderVerify) {
        AppLog::write("transfer", QString("Checksum verification rejected for %1").arg(mSenderDev.getAddress().toString()));
        writePacket(0, PacketType::Cancel, QByteArray());
        mSocket->disconnectFromHost();
        mInfo->fail(TransferFailureReason::ChecksumFailed,
                    tr("Transfer rejected: sender disabled checksum verification."));
        return;
    }
    mVerifyChecksum = Settings::instance()->getVerifyChecksum() && senderVerifyAdvertised && senderVerify;
    if (mVerifyChecksum && !mHash)
        mHash = new QCryptographicHash(QCryptographicHash::Sha256);
    else if (!mVerifyChecksum && mHash) {
        delete mHash;
        mHash = nullptr;
    }

    QString fileName = obj.value("name").toString();
    QString folderName = obj.value("folder").toString();
    QString dstFolderPath = Settings::instance()->getDownloadDir();
    if (!folderName.isEmpty())
        dstFolderPath = dstFolderPath + QDir::separator() + folderName;

    QDir dir(dstFolderPath);
    if (!dir.exists())
        dir.mkpath(dstFolderPath);

    mFinalFilePath = dstFolderPath + QDir::separator() + fileName;
    if (!Settings::instance()->getReplaceExistingFile())
        mFinalFilePath = Util::getUniqueFileName(fileName, dstFolderPath);

    mPartFilePath = mFinalFilePath + ".part";
    mBytesRead = 0;
    mAcceptedStreams = 1;

    if (mResumePartialDownloads && QFile::exists(mPartFilePath)) {
        const qint64 partSize = QFileInfo(mPartFilePath).size();
        if (partSize > 0 && partSize < mFileSize) {
            const QString expectedFingerprint =
                TransferJournal::makeFingerprint(mTransferId, mFileSize, mFinalFilePath);
            const QList<JournalEntry> journalEntries = TransferJournal::instance()->loadAll();
            for (const JournalEntry& entry : journalEntries) {
                if (entry.transferId != mTransferId)
                    continue;
                if (!entry.fingerprint.isEmpty() && entry.fingerprint != expectedFingerprint) {
                    QFile::remove(mPartFilePath);
                    mBytesRead = 0;
                    AppLog::transferEvent(mTransferId, QStringLiteral("resume_reset"),
                                          mSenderDev.getAddress().toString(),
                                          QStringLiteral("journal_mismatch"),
                                          QStringLiteral("0"),
                                          tr("Stale partial file removed due to fingerprint mismatch."));
                    break;
                }
            }
            if (mBytesRead == 0 && partSize > 0 && partSize < mFileSize && QFile::exists(mPartFilePath))
                mBytesRead = partSize;
        } else if (partSize >= mFileSize) {
            QFile::remove(mPartFilePath);
        }
    }

    if (senderParallelSupported && requestedStreams > 1 && mBytesRead == 0)
        mAcceptedStreams = requestedStreams;
    if (mBytesRead > 0 && mAcceptedStreams > 1) {
        mAcceptedStreams = 1;
        AppLog::write("transfer", QString("Parallel resume disabled for transfer %1; falling back to single stream.")
                     .arg(mTransferId));
    }

    const qint64 bytesNeeded = mFileSize - mBytesRead;
    const qint64 available = Util::availableBytes(dstFolderPath);
    if (available >= 0 && bytesNeeded > available) {
        mInfo->setFilePath(mFinalFilePath);
        AppLog::write("transfer", QString("Insufficient space for %1 in %2")
                                       .arg(mFinalFilePath, dstFolderPath));
        writePacket(0, PacketType::Cancel, QByteArray());
        mSocket->disconnectFromHost();
        mInfo->fail(TransferFailureReason::InsufficientSpace,
                    tr("Not enough free space in %1 (need %2, have %3)")
                        .arg(dstFolderPath,
                             Util::sizeToString(bytesNeeded),
                             Util::sizeToString(available)));
        return;
    }

    QString writePath = mResumePartialDownloads ? mPartFilePath : mFinalFilePath;
    mInfo->setFilePath(mFinalFilePath);
    mFile = new QFile(writePath, this);
    QIODevice::OpenMode mode = QIODevice::WriteOnly;
    if (mResumePartialDownloads && mBytesRead > 0)
        mode = QIODevice::ReadWrite;

    if (mFile->open(mode)) {
        if (mBytesRead > 0) {
            mFile->seek(mBytesRead);
            hashExistingPartFile();
            mInfo->setProgress((int)(mBytesRead * 100 / mFileSize));
            mInfo->setBytesTransferred(mBytesRead);
            AppLog::write("transfer", QString("Download resuming at offset %1: %2")
                                           .arg(mBytesRead)
                                           .arg(mFinalFilePath));
        }
        mInfo->setState(TransferState::Transfering);
        journalCheckpointDownload(this, TransferState::Transfering);
        AppLog::transferEvent(mTransferId, QStringLiteral("start"), mSenderDev.getAddress().toString(),
                              QStringLiteral("-"), QStringLiteral("0"), mFinalFilePath);
        if (!mTransferId.isEmpty()) {
            QMutexLocker lock(&gTransferRegistryMutex);
            gTransferRegistry.insert(mTransferId, this);
            mRegisteredTransferId = true;
        }
        emit mInfo->fileOpened();
        sendOffsetAck(mBytesRead, mAcceptedStreams);
    } else {
        mInfo->fail(TransferFailureReason::FileIoError, tr("Failed to write ") + writePath);
        writePacket(0, PacketType::Cancel, QByteArray());
        mSocket->disconnectFromHost();
    }
}

void Receiver::failDownloadWrite(const QString& message)
{
    if (mInfo->getState() == TransferState::Failed || mInfo->getState() == TransferState::Cancelled)
        return;

    writePacket(0, PacketType::Cancel, QByteArray());
    closeDataSockets();
    removePartFile();
    if (mFile) {
        mFile->close();
        mFile->remove();
    }
    if (mRegisteredTransferId) {
        QMutexLocker lock(&gTransferRegistryMutex);
        gTransferRegistry.remove(mTransferId);
        mRegisteredTransferId = false;
    }
    mInfo->fail(TransferFailureReason::FileIoError, message);
    if (mSocket)
        mSocket->disconnectFromHost();
}

void Receiver::processDataPacket(QByteArray& data)
{
    if (mAttachProxy)
        return;
    if (!mFile || mBytesRead + data.size() > mFileSize)
        return;

    const qint64 written = mFile->write(data);
    if (written != data.size()) {
        failDownloadWrite(tr("Failed to write download data to disk."));
        return;
    }
    if (mVerifyChecksum && mHash)
        mHash->addData(data);
    mBytesRead += written;

    mInfo->setProgress( (int)(mBytesRead * 100 / mFileSize) );
    mInfo->setBytesTransferred(mBytesRead);
}

void Receiver::adoptDataSocket(QTcpSocket* socket)
{
    if (!socket)
        return;

    QJsonObject attachedObj({{"stream_attached", true}});
    const QByteArray attachedData = QJsonDocument(attachedObj).toJson(QJsonDocument::Compact);
    qint32 ackPacketSize = attachedData.size();
    const char ackType = static_cast<char>(PacketType::OffsetAck);
    socket->write(reinterpret_cast<const char*>(&ackPacketSize), sizeof(ackPacketSize));
    socket->write(&ackType, sizeof(ackType));
    socket->write(attachedData);
    socket->flush();

    mDataBuffers.insert(socket, QByteArray());
    mDataPacketSizes.insert(socket, -1);

    connect(socket, &QTcpSocket::disconnected, this, [this, socket]() {
        socket->disconnect(this);
        mDataBuffers.remove(socket);
        mDataPacketSizes.remove(socket);
    });
    connect(socket, &QTcpSocket::readyRead, this, [this, socket]() {
        processStripedSocketReadyRead(socket);
    });
}

void Receiver::processStripedSocketReadyRead(QTcpSocket* socket)
{
    if (!socket || !mDataBuffers.contains(socket) || !mDataPacketSizes.contains(socket))
        return;

    mDataBuffers[socket].append(socket->readAll());

    const int headerSize = static_cast<int>(sizeof(qint32) + sizeof(char));
    const qint32 maxPacketSize = Settings::instance()->getMaxPacketSize();

    while (mDataBuffers.contains(socket) && mDataPacketSizes.contains(socket)
           && mDataBuffers[socket].size() >= headerSize) {
        qint32 packetSize = mDataPacketSizes[socket];

        if (packetSize < 0) {
            memcpy(&packetSize, mDataBuffers[socket].constData(), sizeof(packetSize));
            mDataBuffers[socket].remove(0, static_cast<int>(sizeof(packetSize)));
            if (packetSize < 0 || packetSize > maxPacketSize) {
                mDataBuffers.remove(socket);
                mDataPacketSizes.remove(socket);
                mInfo->fail(TransferFailureReason::PacketOversize,
                            tr("Rejected oversized striped packet."));
                socket->disconnectFromHost();
                return;
            }
            mDataPacketSizes[socket] = packetSize;
            continue;
        }

        if (mDataBuffers[socket].size() < packetSize + 1)
            break;

        const PacketType type = static_cast<PacketType>(mDataBuffers[socket].at(0));
        const QByteArray payload = mDataBuffers[socket].mid(1, packetSize);
        if (type == PacketType::StripedData)
            handleStripedPayload(payload);
        mDataBuffers[socket].remove(0, packetSize + 1);
        mDataPacketSizes[socket] = -1;
    }
}

void Receiver::handleStripedPayload(const QByteArray& payload)
{
    if (!mFile || !mFile->isOpen())
        return;
    if (mInfo->getState() == TransferState::Finish || mInfo->getState() == TransferState::Cancelled
        || mInfo->getState() == TransferState::Failed) {
        return;
    }

    QDataStream in(payload);
    in.setByteOrder(QDataStream::LittleEndian);
    qint64 offset = 0;
    qint32 len = 0;
    in >> offset;
    in >> len;
    const QByteArray chunk = payload.mid(sizeof(qint64) + sizeof(qint32), len);
    if (len <= 0 || chunk.size() != len || offset < 0 || offset + len > mFileSize)
        return;

    if (!mFile->seek(offset)) {
        failDownloadWrite(tr("Failed to seek download file for striped write."));
        return;
    }
    const qint64 written = mFile->write(chunk);
    if (written != len) {
        failDownloadWrite(tr("Failed to write striped download data to disk."));
        return;
    }

    const qint64 endOffset = offset + written;
    if (endOffset > mBytesRead)
        mBytesRead = endOffset;
    mInfo->setProgress((int)(mBytesRead * 100 / mFileSize));
    mInfo->setBytesTransferred(mBytesRead);

    if (mFinishPending && mBytesRead >= mFileSize) {
        const QString actual = Util::fileSha256(mFile->fileName()).toLower();
        if (mPendingFinishHash.isEmpty() || actual != mPendingFinishHash) {
            mFile->close();
            removePartFile();
            if (!mResumePartialDownloads && mFile)
                mFile->remove();
            AppLog::write("transfer", QString("Checksum failed: %1").arg(mFinalFilePath));
            mSocket->disconnectFromHost();
            mInfo->fail(TransferFailureReason::ChecksumFailed, tr("Checksum verification failed."));
            return;
        }
        mFinishPending = false;
        mPendingFinishHash.clear();
        finalizeDownload();
    }
}

void Receiver::closeDataSockets()
{
    const auto keys = mDataBuffers.keys();
    for (QTcpSocket* socket : keys) {
        if (!socket)
            continue;
        socket->disconnect(this);
        socket->disconnectFromHost();
        socket->deleteLater();
    }
    mDataBuffers.clear();
    mDataPacketSizes.clear();
}

void Receiver::processFinishPacket(QByteArray& data)
{
    if (mAttachProxy)
        return;
    if (mVerifyChecksum && mHash) {
        const QJsonObject obj = QJsonDocument::fromJson(data).object();
        const QString expected = obj.value("sha256").toString().toLower();
        if (mAcceptedStreams > 1 && mBytesRead < mFileSize) {
            mFinishPending = true;
            mPendingFinishHash = expected;
            return;
        }

        QString actual = (mAcceptedStreams > 1)
                             ? Util::fileSha256(mFile->fileName()).toLower()
                             : QString::fromLatin1(mHash->result().toHex());

        if (expected.isEmpty() || expected != actual) {
            mFile->close();
            removePartFile();
            if (!mResumePartialDownloads && mFile)
                mFile->remove();
            AppLog::write("transfer", QString("Checksum failed: %1").arg(mFinalFilePath));
            mSocket->disconnectFromHost();
            mInfo->fail(TransferFailureReason::ChecksumFailed, tr("Checksum verification failed."));
            return;
        }
    }

    finalizeDownload();
}

void Receiver::processCancelPacket(QByteArray& data)
{
    Q_UNUSED(data);

    mInfo->setState(TransferState::Cancelled);
    mInfo->setProgress(0);
    clearReadBuffer();
    closeDataSockets();
    removePartFile();
    if (mFile) {
        mFile->close();
        mFile->remove();
    }
    if (mRegisteredTransferId) {
        QMutexLocker lock(&gTransferRegistryMutex);
        gTransferRegistry.remove(mTransferId);
        mRegisteredTransferId = false;
    }
    mSocket->disconnectFromHost();
}
