/*
    LANShare - LAN file transfer.
    Copyright (C) 2016 Abdul Aris R. <abdularisrahmanudin10@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#ifndef ABOUTPAGE_H
#define ABOUTPAGE_H

#include <QWidget>

class QLabel;
class QPushButton;
class QTextEdit;

class AboutPage : public QWidget
{
    Q_OBJECT

public:
    explicit AboutPage(QWidget* parent = nullptr);

private:
    void showOverview();
    void showCredits(bool checked);
    void showLicense(bool checked);
    QString loadResourceText(const QString& path);

    QWidget* mOverviewWidget{nullptr};
    QTextEdit* mTextEdit{nullptr};
    QPushButton* mCreditsButton{nullptr};
    QPushButton* mLicenseButton{nullptr};
    QString mCredits;
    QString mLicense;
};

#endif // ABOUTPAGE_H
