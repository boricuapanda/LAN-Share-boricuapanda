/*
    LANShare - LAN file transfer.
    Copyright (C) 2016 Abdul Aris R. <abdularisrahmanudin10@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>

#include "logviewerdialog.h"
#include "log.h"

LogViewerDialog::LogViewerDialog(QWidget* parent)
    : QDialog(parent), mLogView(nullptr), mStatsLabel(nullptr), mWatcher(new QFileSystemWatcher(this))
{
    setupUi();
    connect(mWatcher, &QFileSystemWatcher::fileChanged, this, &LogViewerDialog::onLogFileChanged);
    reloadLog();
}

void LogViewerDialog::setupUi()
{
    setWindowTitle(tr("Transfer Log"));
    resize(760, 480);

    auto* layout = new QVBoxLayout(this);

    auto* pathLabel = new QLabel(AppLog::logFilePath(), this);
    pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    pathLabel->setStyleSheet(QStringLiteral("color: #666;"));
    layout->addWidget(pathLabel);

    mStatsLabel = new QLabel(this);
    mStatsLabel->setWordWrap(true);
    mStatsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    mStatsLabel->setStyleSheet(QStringLiteral("color: #333; font-weight: bold;"));
    layout->addWidget(mStatsLabel);

    mLogView = new QPlainTextEdit(this);
    mLogView->setReadOnly(true);
    mLogView->setLineWrapMode(QPlainTextEdit::NoWrap);
    mLogView->setFont(QFont(QStringLiteral("Monospace"), 9));
    layout->addWidget(mLogView);

    auto* buttons = new QHBoxLayout();
    auto* refreshBtn = new QPushButton(tr("Refresh"), this);
    auto* openFolderBtn = new QPushButton(tr("Open Folder"), this);
    auto* closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setDefault(true);

    buttons->addWidget(refreshBtn);
    buttons->addWidget(openFolderBtn);
    buttons->addStretch();
    buttons->addWidget(closeBtn);
    layout->addLayout(buttons);

    connect(refreshBtn, &QPushButton::clicked, this, &LogViewerDialog::reloadLog);
    connect(openFolderBtn, &QPushButton::clicked, this, &LogViewerDialog::openLogFolder);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
}

void LogViewerDialog::reloadLog()
{
    const QString path = AppLog::logFilePath();
    const QString content = AppLog::readTail();

    if (mWatcher->files() != QStringList{path}) {
        if (!mWatcher->files().isEmpty())
            mWatcher->removePaths(mWatcher->files());
        if (QFile::exists(path))
            mWatcher->addPath(path);
    }

    mLogView->setPlainText(content.isEmpty() ? tr("(Log file is empty or not created yet)") : content);
    mLogView->moveCursor(QTextCursor::End);

    const TransferLogStats stats = AppLog::parseTransferStats(content);
    if (stats.starts == 0 && stats.finishes == 0 && stats.failures == 0
        && stats.retries == 0 && stats.recoveries == 0) {
        mStatsLabel->setText(tr("Reliability summary: no structured transfer events in this log tail."));
    } else {
        mStatsLabel->setText(tr("Reliability summary: %1").arg(AppLog::formatTransferStatsSummary(stats)));
    }
}

void LogViewerDialog::onLogFileChanged()
{
    reloadLog();
    const QString path = AppLog::logFilePath();
    if (QFile::exists(path) && !mWatcher->files().contains(path))
        mWatcher->addPath(path);
}

void LogViewerDialog::openLogFolder()
{
    const QFileInfo info(AppLog::logFilePath());
    QDesktopServices::openUrl(QUrl::fromLocalFile(info.absolutePath()));
}
