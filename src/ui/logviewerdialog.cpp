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
        , mStartColor(QColor(0x1a, 0x6d, 0xaa))
    {
    }

protected:
    void highlightBlock(const QString& text) override
    {
        static const QRegularExpression finishRe(QStringLiteral("phase=finish"));
        static const QRegularExpression failureRe(QStringLiteral("phase=fail(?:ure|ed)"));
        static const QRegularExpression retryRe(QStringLiteral("phase=retry"));
        static const QRegularExpression startRe(QStringLiteral("phase=start"));

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
        apply(startRe, mStartColor);
    }

private:
    QColor mFinishColor;
    QColor mFailureColor;
    QColor mRetryColor;
    QColor mStartColor;
};

} // namespace

LogViewerDialog::LogViewerDialog(QWidget* parent)
    : QDialog(parent)
    , mLogView(nullptr)
    , mStatsLabel(nullptr)
    , mPathLabel(nullptr)
    , mScopeFilter(nullptr)
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
    mPathLabel->setObjectName(QStringLiteral("hintLabel"));
    mPathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(mPathLabel);

    mStatsLabel = new QLabel(this);
    mStatsLabel->setObjectName(QStringLiteral("dialogTitle"));
    mStatsLabel->setWordWrap(true);
    mStatsLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(mStatsLabel);

    auto* filterRow = new QHBoxLayout();
    auto* scopeLabel = new QLabel(tr("Scope:"), this);
    mScopeFilter = new QComboBox(this);
    mScopeFilter->setObjectName(QStringLiteral("scopeFilterComboBox"));
    mScopeFilter->addItem(tr("Current session"), QStringLiteral("session"));
    mScopeFilter->addItem(tr("Full visible tail"), QString());
    auto* filterLabel = new QLabel(tr("Phase:"), this);
    mPhaseFilter = new QComboBox(this);
    mPhaseFilter->setObjectName(QStringLiteral("phaseFilterComboBox"));
    mPhaseFilter->addItem(tr("All"), QString());
    mPhaseFilter->addItem(tr("start"), QStringLiteral("start"));
    mPhaseFilter->addItem(tr("finish"), QStringLiteral("finish"));
    mPhaseFilter->addItem(tr("failure"), QStringLiteral("failed"));
    mPhaseFilter->addItem(tr("retry"), QStringLiteral("retry"));
    mPhaseFilter->addItem(tr("recovery"), QStringLiteral("recovery"));
    filterRow->addWidget(scopeLabel);
    filterRow->addWidget(mScopeFilter);
    filterRow->addSpacing(12);
    filterRow->addWidget(filterLabel);
    filterRow->addWidget(mPhaseFilter);
    filterRow->addStretch();
    layout->addLayout(filterRow);

    mLogView = new QPlainTextEdit(this);
    mLogView->setObjectName(QStringLiteral("logView"));
    mLogView->setReadOnly(true);
    mLogView->setLineWrapMode(QPlainTextEdit::NoWrap);
    mLogView->setFont(QFont(QStringLiteral("Monospace"), 9));
    new LogSyntaxHighlighter(mLogView->document(), palette());
    layout->addWidget(mLogView);

    auto* buttons = new QHBoxLayout();
    auto* refreshBtn = new QPushButton(tr("Refresh"), this);
    auto* openFolderBtn = new QPushButton(tr("Open Folder"), this);
    auto* closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setProperty("primary", true);
    closeBtn->setDefault(true);

    buttons->addWidget(refreshBtn);
    buttons->addWidget(openFolderBtn);
    buttons->addStretch();
    buttons->addWidget(closeBtn);
    layout->addLayout(buttons);

    connect(refreshBtn, &QPushButton::clicked, this, &LogViewerDialog::reloadLog);
    connect(openFolderBtn, &QPushButton::clicked, this, &LogViewerDialog::openLogFolder);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(mScopeFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LogViewerDialog::reloadLog);
    connect(mPhaseFilter, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &LogViewerDialog::reloadLog);
}

QString LogViewerDialog::filterLogContent(const QString& content) const
{
    QString scopedContent = content;
    if (mScopeFilter->currentData().toString() == QLatin1String("session")) {
        const int sessionStart = scopedContent.lastIndexOf(QStringLiteral("tls Qt SSL support="));
        if (sessionStart >= 0) {
            const int lineStart = scopedContent.lastIndexOf(QLatin1Char('\n'), sessionStart);
            scopedContent = scopedContent.mid(lineStart < 0 ? 0 : lineStart + 1);
        }
    }

    const QString phase = mPhaseFilter->currentData().toString();
    if (phase.isEmpty())
        return scopedContent;

    const QString needle = QStringLiteral("phase=%1").arg(phase);
    QStringList filtered;
    const QStringList lines = scopedContent.split(QLatin1Char('\n'));
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
        mStatsLabel->setText(tr("Reliability summary for visible log tail: no structured transfer events."));
    } else {
        mStatsLabel->setText(tr("Reliability summary for visible log tail: %1")
                                 .arg(AppLog::formatTransferStatsSummary(stats)));
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
