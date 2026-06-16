/*
    LANShare - LAN file transfer.
    Copyright (C) 2016 Abdul Aris R. <abdularisrahmanudin10@gmail.com>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "settingsdialog.h"
#include "ui_settingsdialog.h"

#include "settings.h"
#include "util.h"
#include "ui/logviewerdialog.h"
#include "ui/tlstrustdialog.h"
#include "ui/uitheme.h"
#include "log.h"
#include "transfer/tlshelper.h"
#include <QApplication>
#include <QMessageBox>
#include <QLabel>
#include <QScrollArea>
#include <QFrame>
#include <QVBoxLayout>

namespace {

void makeTabsScrollable(QTabWidget* tabWidget)
{
    if (!tabWidget)
        return;

    for (int i = 0; i < tabWidget->count(); ++i) {
        QWidget* page = tabWidget->widget(i);
        if (!page)
            continue;

        const QString title = tabWidget->tabText(i);
        tabWidget->removeTab(i);

        auto* scroll = new QScrollArea(tabWidget);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        scroll->setWidget(page);
        tabWidget->insertTab(i, scroll, title);
    }
}

} // namespace

namespace {

UiThemeMode themeModeFromComboIndex(int index)
{
    switch (index) {
    case 1:
        return UiThemeMode::Light;
    case 2:
        return UiThemeMode::Dark;
    default:
        return UiThemeMode::System;
    }
}

int comboIndexFromThemeMode(UiThemeMode mode)
{
    switch (mode) {
    case UiThemeMode::Light:
        return 1;
    case UiThemeMode::Dark:
        return 2;
    case UiThemeMode::System:
    default:
        return 0;
    }
}

void addUnlimitedHint(QVBoxLayout* layout, QLayout* afterLayout)
{
    const int idx = layout->indexOf(afterLayout);
    if (idx < 0)
        return;

    auto* hint = new QLabel(QObject::tr("0 = unlimited"));
    QFont font = hint->font();
    font.setPointSize(qMax(7, font.pointSize() - 1));
    hint->setFont(font);
    hint->setStyleSheet(QStringLiteral("color: palette(mid);"));
    layout->insertWidget(idx + 1, hint);
}

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent) :
    QDialog(parent),
    ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);
    makeTabsScrollable(ui->tabWidget);
    addUnlimitedHint(ui->verticalLayout_3, ui->horizontalLayout_5);
    addUnlimitedHint(ui->verticalLayout_3, ui->horizontalLayout_maxDownloads);
    addUnlimitedHint(ui->verticalLayout_3, ui->horizontalLayout_retryMax);
    resize(500, 720);
    setMinimumSize(480, 560);
    assign();
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

void SettingsDialog::onCancelClicked()
{
    reject();
}

void SettingsDialog::onSaveClicked()
{
    Settings* set = Settings::instance();

    QString name = ui->deviceNameLineEdit->text();
    if (!name.isEmpty())
        set->setDeviceName(name);

    set->setBroadcastPort(ui->bcPortSpinBox->value());
    set->setTransferPort(ui->transferPortSpinBox->value());
    set->setFileBufferSize(ui->buffSizeSpinBox->value() * 1024);
    set->setDeviceName(ui->deviceNameLineEdit->text());
    set->setDownloadDir(ui->downDirlineEdit->text());
    set->setBroadcastInterval(ui->bcIntervalSpinBox->value());
    set->setReplaceExistingFile(ui->overwriteCheckBox->isChecked());
    set->setVerifyChecksum(ui->verifyChecksumCheckBox->isChecked());
    set->setResumePartialDownloads(ui->resumePartialCheckBox->isChecked());
    set->setMaxConcurrentTransfers(ui->maxTransfersSpinBox->value());
    set->setParallelStreams(ui->parallelStreamsSpinBox->value());
    set->setAuthEnabled(ui->authCheckBox->isChecked());
    set->setAuthToken(ui->authTokenLineEdit->text());
    set->setTlsEnabled(ui->tlsCheckBox->isChecked());
    set->setMaxConcurrentDownloads(ui->maxDownloadsSpinBox->value());
    set->setTransferRetryMax(ui->transferRetryMaxSpinBox->value());
    set->setTransferRetryBaseMs(ui->transferRetryBaseSpinBox->value());
    set->setJournalEnabled(ui->journalEnabledCheckBox->isChecked());
    set->setJournalRetentionDays(ui->journalRetentionSpinBox->value());
    set->setTransferIdleTimeoutMs(ui->transferIdleTimeoutSpinBox->value() * 1000);
    set->setMaxPacketSize(ui->maxPacketSizeSpinBox->value() * 1024);
    set->setTransferOffsetAckTimeoutMs(ui->offsetAckTimeoutSpinBox->value() * 1000);
    set->setUiTheme(themeModeFromComboIndex(ui->themeComboBox->currentIndex()));

    set->saveSettings();
    UiTheme::apply(qApp);

    accept();
}

void SettingsDialog::onResetClicked()
{
    Settings::instance()->reset();
    assign();
    UiTheme::apply(qApp);
}

void SettingsDialog::onSelectDownDirClicked()
{
    const QString dirName = Settings::instance()->getDownloadDir();
    const QString newDirName = Util::selectExistingDirectory(
        this, tr("Select a directory"), dirName);

    if (!newDirName.isEmpty())
        ui->downDirlineEdit->setText(newDirName);
}

void SettingsDialog::onViewLogClicked()
{
    LogViewerDialog dialog(this);
    dialog.exec();
}

void SettingsDialog::onManageTlsTrustClicked()
{
    TlsTrustDialog dialog(this);
    dialog.exec();
    ui->tlsPinnedCountLabel->setText(tr("Pinned peers: %1").arg(TlsHelper::pinnedPeerCount()));
}

void SettingsDialog::assign()
{
    Settings* sets = Settings::instance();
    Device me = sets->getMyDevice();

    ui->deviceIdLabel->setText(me.getId());
    ui->ipAddrLabel->setText(me.getAddress().toString());
    ui->osNameLabel->setText(me.getOSName());
    ui->deviceNameLineEdit->setText(me.getName());
    ui->downDirlineEdit->setText(sets->getDownloadDir());

    ui->bcPortSpinBox->setValue(sets->getBroadcastPort());
    ui->transferPortSpinBox->setValue(sets->getTransferPort());
    ui->buffSizeSpinBox->setValue(sets->getFileBufferSize() / 1024);
    ui->bcIntervalSpinBox->setValue(sets->getBroadcastInterval());
    ui->overwriteCheckBox->setChecked(sets->getReplaceExistingFile());
    ui->verifyChecksumCheckBox->setChecked(sets->getVerifyChecksum());
    ui->resumePartialCheckBox->setChecked(sets->getResumePartialDownloads());
    ui->maxTransfersSpinBox->setValue(sets->getMaxConcurrentTransfers());
    ui->parallelStreamsSpinBox->setValue(sets->getParallelStreams());
    ui->authCheckBox->setChecked(sets->getAuthEnabled());
    ui->authTokenLineEdit->setText(sets->getAuthToken());
    ui->tlsCheckBox->setChecked(sets->getTlsEnabled());
    ui->maxDownloadsSpinBox->setValue(sets->getMaxConcurrentDownloads());
    ui->transferRetryMaxSpinBox->setValue(sets->getTransferRetryMax());
    ui->transferRetryBaseSpinBox->setValue(sets->getTransferRetryBaseMs());
    ui->journalEnabledCheckBox->setChecked(sets->getJournalEnabled());
    ui->journalRetentionSpinBox->setValue(sets->getJournalRetentionDays());
    ui->transferIdleTimeoutSpinBox->setValue(sets->getTransferIdleTimeoutMs() / 1000);
    ui->maxPacketSizeSpinBox->setValue(sets->getMaxPacketSize() / 1024);
    ui->offsetAckTimeoutSpinBox->setValue(sets->getTransferOffsetAckTimeoutMs() / 1000);
    ui->themeComboBox->setCurrentIndex(comboIndexFromThemeMode(sets->getUiTheme()));
    ui->logPathLabel->setText(AppLog::logFilePath());
    ui->tlsPinnedCountLabel->setText(tr("Pinned peers: %1").arg(TlsHelper::pinnedPeerCount()));
}
