/*
    LANShare - LAN file transfer.
    Copyright (C) 2016 Abdul Aris R. <abdularisrahmanudin10@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#ifndef LOGVIEWERDIALOG_H
#define LOGVIEWERDIALOG_H

#include <QDialog>

class QComboBox;
class QLabel;
class QPlainTextEdit;
class QFileSystemWatcher;

class LogViewerDialog : public QDialog
{
    Q_OBJECT

public:
    explicit LogViewerDialog(QWidget* parent = nullptr);

private Q_SLOTS:
    void reloadLog();
    void onLogFileChanged();
    void openLogFolder();

private:
    void setupUi();
    QString filterLogContent(const QString& content) const;

    QPlainTextEdit* mLogView;
    QLabel* mStatsLabel;
    QLabel* mPathLabel;
    QComboBox* mScopeFilter;
    QComboBox* mPhaseFilter;
    QFileSystemWatcher* mWatcher;
};

#endif // LOGVIEWERDIALOG_H
