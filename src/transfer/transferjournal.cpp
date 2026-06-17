/*
    LANShare - LAN file transfer.
    Copyright (C) 2016 Abdul Aris R. <abdularisrahmanudin10@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include "transferjournal.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QFileInfo>
#include <QStringList>
#include <algorithm>

#include "log.h"
#include "settings.h"

TransferJournal* TransferJournal::obj = new TransferJournal;

static QString gJournalPathOverride;

TransferJournal* TransferJournal::instance()
{
    return obj;
}

void TransferJournal::setStoragePathForTests(const QString& path)
{
    gJournalPathOverride = path;
}

void TransferJournal::clearStoragePathForTests()
{
    gJournalPathOverride.clear();
}

QString TransferJournal::journalPath() const
{
    if (!gJournalPathOverride.isEmpty())
        return gJournalPathOverride;

    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return base + QDir::separator() + QStringLiteral("LANShare")
           + QDir::separator() + QStringLiteral("transfer-journal.json");
}

QString TransferJournal::makeFingerprint(const QString& transferId, qint64 dataSize, const QString& filePath)
{
    const QByteArray material = QStringLiteral("%1|%2|%3")
                                    .arg(transferId)
                                    .arg(dataSize)
                                    .arg(filePath)
                                    .toUtf8();
    return QString::fromLatin1(QCryptographicHash::hash(material, QCryptographicHash::Sha256).toHex());
}

static QString transferTypeToString(TransferType type)
{
    switch (type) {
    case TransferType::Download: return QStringLiteral("download");
    case TransferType::Upload: return QStringLiteral("upload");
    default: return QStringLiteral("none");
    }
}

static TransferType transferTypeFromString(const QString& value)
{
    if (value == QLatin1String("download"))
        return TransferType::Download;
    if (value == QLatin1String("upload"))
        return TransferType::Upload;
    return TransferType::None;
}

static QString transferStateToString(TransferState state)
{
    switch (state) {
    case TransferState::Queued: return QStringLiteral("queued");
    case TransferState::Waiting: return QStringLiteral("waiting");
    case TransferState::Transfering: return QStringLiteral("transfering");
    case TransferState::Paused: return QStringLiteral("paused");
    case TransferState::Finish: return QStringLiteral("finish");
    case TransferState::Failed: return QStringLiteral("failed");
    case TransferState::Cancelled: return QStringLiteral("cancelled");
    case TransferState::Disconnected: return QStringLiteral("disconnected");
    default: return QStringLiteral("idle");
    }
}

static TransferState transferStateFromString(const QString& value)
{
    if (value == QLatin1String("queued")) return TransferState::Queued;
    if (value == QLatin1String("waiting")) return TransferState::Waiting;
    if (value == QLatin1String("transfering")) return TransferState::Transfering;
    if (value == QLatin1String("paused")) return TransferState::Paused;
    if (value == QLatin1String("finish")) return TransferState::Finish;
    if (value == QLatin1String("failed")) return TransferState::Failed;
    if (value == QLatin1String("cancelled")) return TransferState::Cancelled;
    if (value == QLatin1String("disconnected")) return TransferState::Disconnected;
    return TransferState::Idle;
}

static JournalEntry entryFromJson(const QJsonObject& obj)
{
    JournalEntry entry;
    entry.transferId = obj.value(QStringLiteral("transfer_id")).toString();
    entry.type = transferTypeFromString(obj.value(QStringLiteral("type")).toString());
    entry.filePath = obj.value(QStringLiteral("file_path")).toString();
    entry.partPath = obj.value(QStringLiteral("part_path")).toString();
    entry.peerAddress = obj.value(QStringLiteral("peer_address")).toString();
    entry.state = transferStateFromString(obj.value(QStringLiteral("state")).toString());
    entry.dataSize = obj.value(QStringLiteral("data_size")).toVariant().toLongLong();
    entry.bytesTransferred = obj.value(QStringLiteral("bytes_transferred")).toVariant().toLongLong();
    entry.fingerprint = obj.value(QStringLiteral("fingerprint")).toString();
    entry.updatedAtMs = obj.value(QStringLiteral("updated_at_ms")).toVariant().toLongLong();
    return entry;
}

static QJsonObject entryToJson(const JournalEntry& entry)
{
    QJsonObject obj;
    obj.insert(QStringLiteral("transfer_id"), entry.transferId);
    obj.insert(QStringLiteral("type"), transferTypeToString(entry.type));
    obj.insert(QStringLiteral("file_path"), entry.filePath);
    obj.insert(QStringLiteral("part_path"), entry.partPath);
    obj.insert(QStringLiteral("peer_address"), entry.peerAddress);
    obj.insert(QStringLiteral("state"), transferStateToString(entry.state));
    obj.insert(QStringLiteral("data_size"), entry.dataSize);
    obj.insert(QStringLiteral("bytes_transferred"), entry.bytesTransferred);
    obj.insert(QStringLiteral("fingerprint"), entry.fingerprint);
    obj.insert(QStringLiteral("updated_at_ms"), entry.updatedAtMs);
    return obj;
}

QList<JournalEntry> TransferJournal::loadAll() const
{
    QList<JournalEntry> entries;
    QFile file(journalPath());
    if (!file.open(QIODevice::ReadOnly))
        return entries;

    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (!doc.isObject())
        return entries;

    const QJsonArray array = doc.object().value(QStringLiteral("entries")).toArray();
    for (const QJsonValue& value : array) {
        if (!value.isObject())
            continue;
        const JournalEntry entry = entryFromJson(value.toObject());
        if (!entry.transferId.isEmpty())
            entries.append(entry);
    }
    return entries;
}

bool TransferJournal::saveAll(const QList<JournalEntry>& entries) const
{
    if (!Settings::instance()->getJournalEnabled())
        return true;

    QJsonArray array;
    for (const JournalEntry& entry : entries)
        array.append(entryToJson(entry));

    const QJsonObject root({
        {QStringLiteral("version"), 1},
        {QStringLiteral("entries"), array}
    });

    const QString path = journalPath();
    QDir().mkpath(QFileInfo(path).absolutePath());

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;

    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return true;
}

bool TransferJournal::upsert(const JournalEntry& entry)
{
    if (!Settings::instance()->getJournalEnabled() || entry.transferId.isEmpty())
        return false;

    QList<JournalEntry> entries = loadAll();
    bool found = false;
    for (JournalEntry& existing : entries) {
        if (existing.transferId == entry.transferId) {
            existing = entry;
            found = true;
            break;
        }
    }
    if (!found)
        entries.append(entry);

    return saveAll(entries);
}

bool TransferJournal::remove(const QString& transferId)
{
    if (transferId.isEmpty())
        return false;

    QList<JournalEntry> entries = loadAll();
    const int before = entries.size();
    entries.erase(std::remove_if(entries.begin(), entries.end(),
                                 [&](const JournalEntry& e) { return e.transferId == transferId; }),
                  entries.end());
    if (entries.size() == before)
        return true;
    return saveAll(entries);
}

int TransferJournal::recoverOnStartup(QString* summary)
{
    if (!Settings::instance()->getJournalEnabled()) {
        if (summary)
            *summary = QStringLiteral("journal disabled");
        return 0;
    }

    QList<JournalEntry> entries = loadAll();
    int recovered = 0;
    int discarded = 0;
    QList<JournalEntry> kept;

    const qint64 retentionMs = qint64(Settings::instance()->getJournalRetentionDays()) * 24 * 60 * 60 * 1000;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();

    for (const JournalEntry& entry : entries) {
        if (retentionMs > 0 && entry.updatedAtMs > 0 && now - entry.updatedAtMs > retentionMs)
            continue;

        if (entry.state == TransferState::Finish
            || entry.state == TransferState::Cancelled
            || entry.state == TransferState::Failed) {
            ++discarded;
            continue;
        }

        if (entry.type == TransferType::Upload) {
            ++discarded;
            continue;
        }

        if (entry.type == TransferType::Download
            && (!QFileInfo::exists(entry.partPath) || entry.bytesTransferred <= 0)) {
            ++discarded;
            continue;
        }

        kept.append(entry);
        ++recovered;
        AppLog::transferEvent(entry.transferId,
                              QStringLiteral("recovery"),
                              entry.peerAddress,
                              transferStateToString(entry.state),
                              QStringLiteral("journal_mismatch"),
                              QStringLiteral("incomplete transfer retained for resume"));
    }

    saveAll(kept);

    if (summary) {
        if (recovered > 0 || discarded > 0) {
            QStringList parts;
            if (recovered > 0)
                parts << QStringLiteral("retained %1 incomplete transfer(s)").arg(recovered);
            if (discarded > 0)
                parts << QStringLiteral("discarded %1 stale transfer checkpoint(s)").arg(discarded);
            *summary = parts.join(QStringLiteral("; "));
        } else {
            summary->clear();
        }
    }
    return recovered;
}
