/*
    LANShare - LAN file transfer.
    Copyright (C) 2016 Abdul Aris R. <abdularisrahmanudin10@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#ifndef TRANSFERJOURNAL_H
#define TRANSFERJOURNAL_H

#include <QList>
#include <QString>

#include "model/transferinfo.h"

struct JournalEntry {
    QString transferId;
    TransferType type{TransferType::None};
    QString filePath;
    QString partPath;
    QString peerAddress;
    TransferState state{TransferState::Idle};
    qint64 dataSize{0};
    qint64 bytesTransferred{0};
    QString fingerprint;
    qint64 updatedAtMs{0};
};

class TransferJournal
{
public:
    static TransferJournal* instance();

    static void setStoragePathForTests(const QString& path);
    static void clearStoragePathForTests();

    QString journalPath() const;
    QList<JournalEntry> loadAll() const;
    bool upsert(const JournalEntry& entry);
    bool remove(const QString& transferId);
    int recoverOnStartup(QString* summary = nullptr);

    static QString makeFingerprint(const QString& transferId, qint64 dataSize, const QString& filePath);

private:
    TransferJournal() = default;
    bool saveAll(const QList<JournalEntry>& entries) const;

    static TransferJournal* obj;
};

#endif // TRANSFERJOURNAL_H
