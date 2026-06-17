/*
    LANShare - LAN file transfer.
    Copyright (C) 2016 Abdul Aris R. <abdularisrahmanudin10@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
*/

#include "aboutpage.h"

#include <QFile>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextEdit>
#include <QVBoxLayout>

#include "settings.h"
#include "ui/uitheme.h"
#include "util.h"

namespace {

QLabel* makeLabel(const QString& text, const QString& objectName, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setObjectName(objectName);
    label->setWordWrap(true);
    return label;
}

} // namespace

AboutPage::AboutPage(QWidget* parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("aboutPanel"));

    auto* pageLayout = new QVBoxLayout(this);
    pageLayout->setContentsMargins(28, 24, 28, 28);
    pageLayout->setSpacing(18);

    auto* heroCard = new QFrame(this);
    heroCard->setObjectName(QStringLiteral("aboutHeroCard"));
    auto* heroLayout = new QVBoxLayout(heroCard);
    heroLayout->setContentsMargins(28, 26, 28, 24);
    heroLayout->setSpacing(14);

    auto* iconLabel = new QLabel(heroCard);
    iconLabel->setObjectName(QStringLiteral("aboutAppIcon"));
    iconLabel->setPixmap(UiTheme::appIcon(QStringLiteral(":/img/icon.png")).pixmap(56, 56));
    iconLabel->setAlignment(Qt::AlignCenter);
    heroLayout->addWidget(iconLabel);

    auto* appName = makeLabel(PROGRAM_NAME, QStringLiteral("aboutAppName"), heroCard);
    appName->setAlignment(Qt::AlignCenter);
    heroLayout->addWidget(appName);

    auto* version = makeLabel(Util::parseAppVersion(false), QStringLiteral("aboutVersionBadge"), heroCard);
    version->setAlignment(Qt::AlignCenter);
    heroLayout->addWidget(version, 0, Qt::AlignHCenter);

    auto* description = makeLabel(PROGRAM_DESC, QStringLiteral("aboutDescription"), heroCard);
    description->setAlignment(Qt::AlignCenter);
    heroLayout->addWidget(description);

    mOverviewWidget = new QWidget(heroCard);
    auto* overviewLayout = new QVBoxLayout(mOverviewWidget);
    overviewLayout->setContentsMargins(0, 8, 0, 0);
    overviewLayout->setSpacing(8);

    auto* localFocus = makeLabel(
        tr("Built for simple, local network file transfers between your own machines."),
        QStringLiteral("aboutBodyText"),
        mOverviewWidget);
    localFocus->setAlignment(Qt::AlignCenter);
    overviewLayout->addWidget(localFocus);

    auto* copyright = makeLabel(
        tr("Copyright (c) 2016, Abdul Aris R."),
        QStringLiteral("aboutMutedText"),
        mOverviewWidget);
    copyright->setAlignment(Qt::AlignCenter);
    overviewLayout->addWidget(copyright);

    heroLayout->addWidget(mOverviewWidget);

    mTextEdit = new QTextEdit(heroCard);
    mTextEdit->setObjectName(QStringLiteral("aboutTextView"));
    mTextEdit->setReadOnly(true);
    mTextEdit->setVisible(false);
    mTextEdit->setTextInteractionFlags(Qt::TextBrowserInteraction);
    heroLayout->addWidget(mTextEdit, 1);

    auto* buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(8);
    buttonRow->addStretch();

    mCreditsButton = new QPushButton(tr("Credits"), heroCard);
    mCreditsButton->setObjectName(QStringLiteral("aboutToggleButton"));
    mCreditsButton->setCheckable(true);
    mCreditsButton->setAutoDefault(false);
    buttonRow->addWidget(mCreditsButton);

    mLicenseButton = new QPushButton(tr("License"), heroCard);
    mLicenseButton->setObjectName(QStringLiteral("aboutToggleButton"));
    mLicenseButton->setCheckable(true);
    mLicenseButton->setAutoDefault(false);
    buttonRow->addWidget(mLicenseButton);
    buttonRow->addStretch();
    heroLayout->addLayout(buttonRow);

    pageLayout->addWidget(heroCard);
    pageLayout->addStretch();

    connect(mCreditsButton, &QPushButton::toggled, this, &AboutPage::showCredits);
    connect(mLicenseButton, &QPushButton::toggled, this, &AboutPage::showLicense);
}

void AboutPage::showOverview()
{
    if (mOverviewWidget)
        mOverviewWidget->show();
    if (mTextEdit)
        mTextEdit->hide();
}

void AboutPage::showCredits(bool checked)
{
    if (!checked) {
        if (!mLicenseButton || !mLicenseButton->isChecked())
            showOverview();
        return;
    }

    if (mLicenseButton)
        mLicenseButton->setChecked(false);
    if (mCredits.isEmpty())
        mCredits = loadResourceText(QStringLiteral(":/text/credits.html"));
    if (mTextEdit)
        mTextEdit->setText(mCredits);
    if (mOverviewWidget)
        mOverviewWidget->hide();
    if (mTextEdit)
        mTextEdit->show();
}

void AboutPage::showLicense(bool checked)
{
    if (!checked) {
        if (!mCreditsButton || !mCreditsButton->isChecked())
            showOverview();
        return;
    }

    if (mCreditsButton)
        mCreditsButton->setChecked(false);
    if (mLicense.isEmpty())
        mLicense = loadResourceText(QStringLiteral(":/text/gpl-3.0.txt"));
    if (mTextEdit)
        mTextEdit->setText(mLicense);
    if (mOverviewWidget)
        mOverviewWidget->hide();
    if (mTextEdit)
        mTextEdit->show();
}

QString AboutPage::loadResourceText(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return tr("Unable to load %1").arg(path);

    return QString::fromUtf8(file.readAll());
}
