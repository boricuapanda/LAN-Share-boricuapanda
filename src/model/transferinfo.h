/*
    LANShare - LAN file transfer.
    Copyright (C) 2016 Abdul Aris R. <abdularisrahmanudin10@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#ifndef TRANSFERINFO_H
#define TRANSFERINFO_H

#include <QElapsedTimer>
#include <QObject>

#include "device.h"
#include "transferfailure.h"

enum class TransferState {
    Idle,
    Queued,
    Waiting,
    Disconnected,
    Paused,
    Cancelled,
    Transfering,
    Finish,
    Failed
};

enum class TransferType {
    None,
    Download,
    Upload
};

class Transfer;

class TransferInfo : public QObject
{
    Q_OBJECT

public:
    explicit TransferInfo(Transfer* owner, QObject *parent = nullptr);

    inline Device getPeer() const { return mPeer; }
    inline int getProgress() const { return mProgress; }
    inline TransferState getState() const { return mState; }
    inline TransferState getLastState() const { return mLastState; }
    inline TransferType getTransferType() const { return mType; }
    inline qint64 getDataSize() const { return mDataSize; }
    inline qint64 getBytesTransferred() const { return mBytesTransferred; }
    inline QString getFilePath() const { return mFilePath; }
    inline Transfer* getOwner() const { return mOwner; }
    inline TransferFailureReason getFailureReason() const { return mFailureReason; }
    inline int getAttempt() const { return mAttempt; }
    inline QString getTransferId() const { return mTransferId; }

    QString getSpeedText() const;
    QString getEtaText() const;

    bool canResume() const;
    bool canPause() const;
    bool canCancel() const;

    void setPeer(Device peer);
    void setState(TransferState state);
    void setTransferType(TransferType type);
    void setProgress(int progress);
    void setBytesTransferred(qint64 bytes);
    void setDataSize(qint64 size);
    void setFilePath(const QString& fileName);
    void setAttempt(int attempt);
    void setTransferId(const QString& transferId);
    void fail(TransferFailureReason reason, const QString& message);
    void resetForRetry();

Q_SIGNALS:
    void done();
    void errorOcurred(const QString& errStr);
    void progressChanged(int progress);
    void statsChanged();
    void fileOpened();
    void stateChanged(TransferState state);

private:
    void resetSpeedTracking();

    Device mPeer;
    TransferState mState;
    TransferState mLastState;
    TransferType mType;
    int mProgress;
    qint64 mDataSize;
    qint64 mBytesTransferred;
    double mSpeedBps;
    QElapsedTimer mSpeedTimer;
    QString mFilePath;
    TransferFailureReason mFailureReason{TransferFailureReason::None};
    int mAttempt{0};
    QString mTransferId;

    Transfer* mOwner;
};

#endif // TRANSFERINFO_H
