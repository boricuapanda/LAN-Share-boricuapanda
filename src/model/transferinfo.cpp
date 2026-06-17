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


#include "transferinfo.h"
#include "util.h"
#include "log.h"

TransferInfo::TransferInfo(Transfer* owner, QObject *parent) :
    QObject(parent),
    mState(TransferState::Idle), mLastState(TransferState::Idle),
    mType(TransferType::None), mProgress(0), mDataSize(0), mBytesTransferred(0),
    mSpeedBps(0.0),
    mOwner(owner)
{
}

bool TransferInfo::canResume() const
{
    return mState == TransferState::Paused;
}

bool TransferInfo::canPause() const
{
    return mState == TransferState::Waiting ||
            mState == TransferState::Transfering;
}

bool TransferInfo::canCancel() const
{
    return mState == TransferState::Queued ||
            mState == TransferState::Waiting ||
            mState == TransferState::Transfering ||
            mState == TransferState::Paused;
}

void TransferInfo::setPeer(Device peer)
{
    mPeer = std::move(peer);
}

void TransferInfo::setState(TransferState newState)
{
    if (newState != mState) {
        TransferState tmp = mState;

        switch (mState) {
        case TransferState::Idle : {
            if (newState == TransferState::Waiting ||
                    newState == TransferState::Queued) {
                mState = newState;
                emit stateChanged(mState);
            }
            break;
        }
        case TransferState::Queued : {
            if (newState == TransferState::Waiting ||
                    newState == TransferState::Cancelled) {
                mState = newState;
                emit stateChanged(mState);
            }
            break;
        }
        case TransferState::Waiting : {
            if (newState == TransferState::Transfering ||
                    newState == TransferState::Cancelled ||
                    newState == TransferState::Paused ||
                    newState == TransferState::Failed) {
                if (newState == TransferState::Transfering)
                    resetSpeedTracking();
                mState = newState;
                emit stateChanged(mState);
            }
            break;
        }
        case TransferState::Transfering : {
            if (newState == TransferState::Disconnected ||
                    newState == TransferState::Finish ||
                    newState == TransferState::Cancelled ||
                    newState == TransferState::Paused ||
                    newState == TransferState::Failed) {
                mState = newState;
                emit stateChanged(mState);
            }
            break;
        }
        case TransferState::Paused : {
            if (newState == TransferState::Waiting ||
                    newState == TransferState::Transfering) {
                if (newState == TransferState::Transfering)
                    resetSpeedTracking();
                mState = mLastState;
                emit stateChanged(mState);
            }
            else if (newState == TransferState::Cancelled ||
                     newState == TransferState::Disconnected) {
                mState = newState;
                emit stateChanged(mState);
            }
            break;
        }
        default:
            break;
        }

        mLastState = tmp;
    }
}

void TransferInfo::setProgress(int newProgress)
{
    if (newProgress != mProgress) {
        mProgress = newProgress;
        emit progressChanged(mProgress);
    }
}

void TransferInfo::setBytesTransferred(qint64 bytes)
{
    if (bytes < 0)
        return;

    mBytesTransferred = bytes;
    if (!mSpeedTimer.isValid())
        mSpeedTimer.start();

    const qint64 elapsedMs = mSpeedTimer.elapsed();
    if (elapsedMs > 0)
        mSpeedBps = bytes * 1000.0 / elapsedMs;

    if (elapsedMs - mLastStatsEmitMs >= 250 || bytes >= mDataSize) {
        mLastStatsEmitMs = elapsedMs;
        emit statsChanged();
    }
}

QString TransferInfo::getSpeedText() const
{
    return Util::formatSpeed(mSpeedBps);
}

QString TransferInfo::getEtaText() const
{
    if (mSpeedBps < 1.0 || mDataSize <= 0 || mBytesTransferred >= mDataSize)
        return Util::formatEta(-1);

    const qint64 remainingBytes = mDataSize - mBytesTransferred;
    const qint64 etaSeconds = (qint64)(remainingBytes / mSpeedBps);
    return Util::formatEta(etaSeconds);
}

void TransferInfo::resetSpeedTracking()
{
    mBytesTransferred = 0;
    mLastStatsEmitMs = 0;
    mSpeedBps = 0.0;
    mSpeedTimer.invalidate();
}

void TransferInfo::setTransferType(TransferType type)
{
    mType = type;
}

void TransferInfo::setDataSize(qint64 size)
{
    mDataSize = size;
}

void TransferInfo::setFilePath(const QString &fileName)
{
    mFilePath = fileName;
}

void TransferInfo::setAttempt(int attempt)
{
    mAttempt = qMax(0, attempt);
}

void TransferInfo::setTransferId(const QString& transferId)
{
    mTransferId = transferId;
}

void TransferInfo::fail(TransferFailureReason reason, const QString& message)
{
    if (mState == TransferState::Finish || mState == TransferState::Cancelled)
        return;

    mFailureReason = reason;
    if (mState != TransferState::Failed) {
        mState = TransferState::Failed;
        emit stateChanged(mState);
    }
    AppLog::transferEvent(mTransferId,
                          QStringLiteral("failed"),
                          mPeer.displayAddress(),
                          transferFailureReasonCode(reason),
                          QString::number(mAttempt),
                          message);
    emit errorOcurred(message);
}

void TransferInfo::resetForRetry()
{
    mFailureReason = TransferFailureReason::None;
    mState = TransferState::Idle;
    mLastState = TransferState::Idle;
    mProgress = 0;
    resetSpeedTracking();
}
