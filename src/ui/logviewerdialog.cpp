/*
    LANShare - LAN file transfer.
    Copyright (C) 2016 Abdul Aris R. <abdularisrahmanudin10@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include <QFont>
#include <QComboBox>
#include <QDesktopServices>
#include <QFile>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QHBoxLayout>
#include <QLabel>
#include <QPalette>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QSyntaxHighlighter>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QUrl>
#include <QVBoxLayout>

#include "logviewerdialog.h"
#include "log.h"

namespace {

class LogSyntaxHighlighter : public QSyntaxHighlighter
{
public:
    LogSyntaxHighlighter(QTextDocument* document, const QPalette& palette)
        : QSyntaxHighlighter(document)
        , mFinishColor(palette.color(QPalette::Link))
        , mFailureColor(palette.color(QPalette::BrightText))
        , mRetryColor(palette.color(QPalette::LinkVisited))
    {
    }

protected:
    void highlightBlock(const QString& text) override
    {
        static const QRegularExpression finishRe(QStringLiteral("phase=finish"));
        static const QRegularExpression failureRe(QStringLiteral("phase=fail(?:ure|ed)"));
        static const QRegularExpression retryRe(QStringLiteral("phase=retry"));

        auto apply = [&](const QRegularExpression& re, const QColor& color) {
            QRegularExpressionMatchIterator it = re.globalMatch(text);
            QTextCharFormat format;
            format.setForeground(color);
            while (it.hasNext()) {
                const QRegularExpressionMatch match = it.next();
                setFormat(match.capturedStart(), match.capturedLength(), format);
            }
        };

        apply(finishRe, mFinishColor);
        apply(failureRe, mFailureColor);
        apply(retryRe, mRetryColor);
    }

private:
    QColor mFinishColor;
    QColor mFailureColor;
    QColor mRetryColor;
};

} // namespace

LogViewerDialog::LogViewerDialog(QWidget* parent)
    : QDialog(parent)
    , mLogView(nullptr)
    , mStatsLabel(nullptr)
    , mPathLabel(nullptr)
    , mPhaseFilter(nullptr)
    , mWatcher(new QFileSystemWatcher(this))
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

    mPathLabel = new QLabel(AppLog::logFilePath(), this);
    mPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    {
        QPalette pal = mPathLabel->palette();
        pal.setColor(QPalette::WindowText, palette().color(QPalette::PlaceholderText));
        mPathLabel->setPalette(pal);
    }
    layout->addWidget(mPathLabel);

    mStatsLabel = new QLabel(this);
    mStatsLabel->setWordWrap(true);
    mStatsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    {
        QPalette pal = mStatsLabel->palette();
        pal.setColor(QPalette::WindowText, palette().color(QPalette::WindowText));
        mStatsLabel->setPalette(pal);
        QFont font = mStatsLabel->font();
        font.setBold(true);
        mStatsLabel->setFont(font);
    }
    layout->addWidget(mStatsLabel);

    auto* filterRow = new QHBoxLayout();
    auto* filterLabel = new QLabel(tr("Phase:"), this);
    mPhaseFilter = new QComboBox(this);
    mPhaseFilter->setObjectName(QStringLiteral("phaseFilterComboBox"));
    mPhaseFilter->addItem(tr("All"), QString());
    mPhaseFilter->addItem(tr("start"), QStringLiteral("start"));
    mPhaseFilter->addItem(tr("finish"), QStringLiteral("finish"));
    mPhaseFilter->addItem(tr("failure"), QStringLiteral("failed"));
    mPhaseFilter->addItem(tr("retry"), QStringLiteral("retry"));
    mPhaseFilter->addItem(tr("recovery"), QStringLiteral("recovery"));
    filterRow->addWidget(filterLabel);
    filterRow->addWidget(mPhaseFilter);
    filterRow->addStretch();
    layout->addLayout(filterRow);

    mLogView = new QPlainTextEdit(this);
    mLogView->setReadOnly(true);
    mLogView->setLineWrapMode(QPlainTextEdit::NoWrap);
    mLogView->setFont(QFont(QStringLiteral("Monospace"), 9));
    new LogSyntaxHighlighter(mLogView->document(), palette());
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
    connect(mPhaseFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LogViewerDialog::reloadLog);
}

QString LogViewerDialog::filterLogContent(const QString& content) const
{
    const QString phase = mPhaseFilter->currentData().toString();
    if (phase.isEmpty())
        return content;

    const QString needle = QStringLiteral("phase=%1").arg(phase);
    QStringList filtered;
    const QStringList lines = content.split(QLatin1Char('\n'));
    for (const QString& line : lines) {
        if (line.contains(needle))
            filtered.append(line);
    }

    return filtered.join(QLatin1Char('\n'));
}

void LogViewerDialog::reloadLog()
{
    const QString path = AppLog::logFilePath();
    const QString rawContent = AppLog::readTail();
    const QString content = filterLogContent(rawContent);

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
