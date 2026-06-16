/*
    LANShare - LAN file transfer.
    Copyright (C) 2016 Abdul Aris R. <abdularisrahmanudin10@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTextStream>
#include <QtDebug>

#include <cstdio>

#include "log.h"

namespace {

QFile gLogFile;
QMutex gLogMutex;
QtMessageHandler gPreviousHandler = nullptr;

QString logDirectory()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return base + QDir::separator() + "LANShare";
}

void ensureLogFileOpen()
{
    if (gLogFile.isOpen())
        return;

    QDir().mkpath(logDirectory());
    gLogFile.setFileName(logDirectory() + QDir::separator() + "lanshare.log");
    gLogFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

void messageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    Q_UNUSED(context);

    const char* level = "INFO";
    switch (type) {
    case QtDebugMsg: level = "DEBUG"; break;
    case QtInfoMsg: level = "INFO"; break;
    case QtWarningMsg: level = "WARN"; break;
    case QtCriticalMsg: level = "ERROR"; break;
    case QtFatalMsg: level = "FATAL"; break;
    }

    const QString line = QString("[%1] [%2] %3")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODate),
                                  level,
                                  msg);

    {
        QMutexLocker lock(&gLogMutex);
        ensureLogFileOpen();
        if (gLogFile.isOpen()) {
            QTextStream stream(&gLogFile);
            stream << line << '\n';
            stream.flush();
        }
    }

    if (gPreviousHandler)
        gPreviousHandler(type, context, msg);
    else if (type >= QtWarningMsg)
        fprintf(stderr, "%s\n", qPrintable(line));
}

} // namespace

void AppLog::install()
{
    QMutexLocker lock(&gLogMutex);
    if (!gPreviousHandler)
        gPreviousHandler = qInstallMessageHandler(messageHandler);
}

void AppLog::write(const QString& category, const QString& message)
{
    qInfo().noquote() << category << message;
}

void AppLog::transferEvent(const QString& transferId,
                           const QString& phase,
                           const QString& peerId,
                           const QString& errorCode,
                           const QString& attempt,
                           const QString& message)
{
    const QString line = QString("transfer_id=%1 phase=%2 peer=%3 code=%4 attempt=%5 msg=%6")
                             .arg(transferId.isEmpty() ? QStringLiteral("-") : transferId,
                                  phase,
                                  peerId.isEmpty() ? QStringLiteral("-") : peerId,
                                  errorCode.isEmpty() ? QStringLiteral("-") : errorCode,
                                  attempt.isEmpty() ? QStringLiteral("0") : attempt,
                                  message);
    write(QStringLiteral("transfer"), line);
}

QString AppLog::logFilePath()
{
    return logDirectory() + QDir::separator() + "lanshare.log";
}

QString AppLog::readTail(int maxBytes)
{
    const QString path = logFilePath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    if (file.size() <= maxBytes)
        return QString::fromUtf8(file.readAll());

    if (!file.seek(file.size() - maxBytes))
        return QString();

    QByteArray tail = file.readAll();
    const int newline = tail.indexOf('\n');
    if (newline >= 0)
        tail.remove(0, newline + 1);

    return QString::fromUtf8(tail);
}

namespace {

void ingestTransferLine(const QString& line, TransferLogStats* stats)
{
    if (!stats || !line.contains(QStringLiteral("transfer_id=")))
        return;

    static const QRegularExpression phaseRe(QStringLiteral("phase=([^\\s]+)"));
    static const QRegularExpression codeRe(QStringLiteral("code=([^\\s]+)"));

    const QRegularExpressionMatch phaseMatch = phaseRe.match(line);
    if (!phaseMatch.hasMatch())
        return;

    const QString phase = phaseMatch.captured(1);
    if (phase == QLatin1String("start"))
        ++stats->starts;
    else if (phase == QLatin1String("finish"))
        ++stats->finishes;
    else if (phase == QLatin1String("failed"))
        ++stats->failures;
    else if (phase == QLatin1String("retry"))
        ++stats->retries;
    else if (phase == QLatin1String("recovery"))
        ++stats->recoveries;

    if (phase == QLatin1String("failed")) {
        const QRegularExpressionMatch codeMatch = codeRe.match(line);
        const QString code = codeMatch.hasMatch() ? codeMatch.captured(1) : QStringLiteral("unknown");
        stats->failuresByCode[code] += 1;
    }
}

} // namespace

TransferLogStats AppLog::parseTransferStats(const QString& logText)
{
    TransferLogStats stats;
    const QStringList lines = logText.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString& line : lines) {
        if (!line.contains(QStringLiteral("transfer_id=")))
            continue;
        ingestTransferLine(line, &stats);
    }
    return stats;
}

TransferLogStats AppLog::parseTransferStatsFromFile()
{
    return parseTransferStats(readTail(1024 * 1024));
}

QString AppLog::formatTransferStatsSummary(const TransferLogStats& stats)
{
    QStringList parts;
    parts << QObject::tr("Starts: %1").arg(stats.starts);
    parts << QObject::tr("Finishes: %1").arg(stats.finishes);
    parts << QObject::tr("Failures: %1").arg(stats.failures);
    parts << QObject::tr("Retries: %1").arg(stats.retries);
    parts << QObject::tr("Recoveries: %1").arg(stats.recoveries);

    if (!stats.failuresByCode.isEmpty()) {
        QStringList codes;
        for (auto it = stats.failuresByCode.constBegin(); it != stats.failuresByCode.constEnd(); ++it)
            codes << QStringLiteral("%1 (%2)").arg(it.key()).arg(it.value());
        codes.sort();
        parts << QObject::tr("Errors: %1").arg(codes.join(QStringLiteral(", ")));
    }

    return parts.join(QStringLiteral(" | "));
}
