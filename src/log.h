/*
    LANShare - LAN file transfer.
    Copyright (C) 2016 Abdul Aris R. <abdularisrahmanudin10@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#ifndef LOG_H
#define LOG_H

#include <QHash>
#include <QString>

struct TransferLogStats {
    int starts = 0;
    int finishes = 0;
    int failures = 0;
    int retries = 0;
    int recoveries = 0;
    QHash<QString, int> failuresByCode;
};

namespace AppLog
{
void install();
void write(const QString& category, const QString& message);
void transferEvent(const QString& transferId,
                   const QString& phase,
                   const QString& peerId,
                   const QString& errorCode,
                   const QString& attempt,
                   const QString& message);
QString logFilePath();
QString readTail(int maxBytes = 256 * 1024);
TransferLogStats parseTransferStats(const QString& logText);
TransferLogStats parseTransferStatsFromFile();
QString formatTransferStatsSummary(const TransferLogStats& stats);
}

#endif // LOG_H
