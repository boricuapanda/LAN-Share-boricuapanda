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
#include <QButtonGroup>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QTabBar>
#include <QTabWidget>
#include <QUuid>
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
    hint->setObjectName(QStringLiteral("hintLabel"));
    QFont font = hint->font();
    font.setPointSize(qMax(7, font.pointSize() - 1));
    hint->setFont(font);
    layout->insertWidget(idx + 1, hint);
}

void reparentLayoutWidgets(QLayout* layout, QWidget* parent)
{
    if (!layout || !parent)
        return;

    for (int i = 0; i < layout->count(); ++i) {
        QLayoutItem* item = layout->itemAt(i);
        if (!item)
            continue;
        if (QWidget* widget = item->widget())
            widget->setParent(parent);
        if (QLayout* childLayout = item->layout())
            reparentLayoutWidgets(childLayout, parent);
    }
}

void moveWidget(QVBoxLayout* from, QVBoxLayout* to, QWidget* widget)
{
    if (!from || !to || !widget || from->indexOf(widget) < 0)
        return;

    from->removeWidget(widget);
    widget->setParent(to->parentWidget());
    to->addWidget(widget);
}

void moveLayout(QVBoxLayout* from, QVBoxLayout* to, QLayout* layout)
{
    if (!from || !to || !layout || from->indexOf(layout) < 0)
        return;

    from->removeItem(layout);
    to->addLayout(layout);
    reparentLayoutWidgets(layout, to->parentWidget());
}

QWidget* makeTabPage(QWidget* card, QWidget* secondCard = nullptr)
{
    auto* page = new QWidget();
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(18);
    layout->addWidget(card);
    if (secondCard)
        layout->addWidget(secondCard);
    layout->addStretch();
    return page;
}

} // namespace

SettingsDialog::SettingsDialog(QWidget *parent) :
    QWidget(parent),
    ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    ui->verticalLayout_6->setContentsMargins(14, 14, 14, 14);
    ui->verticalLayout_6->setSpacing(16);
    setupStitchUi();
    ui->pushButton->setProperty("primary", true);
    makeTabsScrollable(ui->tabWidget);
    if (mReliabilityLayout) {
        addUnlimitedHint(mReliabilityLayout, ui->horizontalLayout_5);
        addUnlimitedHint(mReliabilityLayout, ui->horizontalLayout_maxDownloads);
        addUnlimitedHint(mReliabilityLayout, ui->horizontalLayout_retryMax);
    }
    assign();
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

void SettingsDialog::setupStitchUi()
{
    ui->pushButton_3->setText(tr("Reset defaults"));
    ui->pushButton_3->setObjectName(QStringLiteral("resetDefaultsBtn"));
    ui->pushButton_2->setText(tr("Cancel"));
    ui->pushButton->setText(tr("Save Changes"));

    ui->tabWidget->setDocumentMode(true);
    ui->tabWidget->setUsesScrollButtons(false);
    ui->tabWidget->tabBar()->setUsesScrollButtons(false);
    ui->tabWidget->tabBar()->setExpanding(true);
    ui->tabWidget->tabBar()->setDrawBase(false);
    ui->tabWidget->tabBar()->setElideMode(Qt::ElideNone);

    reorganizeSettingsTabs();
    styleSettingsCards();
    addNetworkPortHints();
    addNetworkInfoBanner();
    setupParallelStreamsBadge();
    setupAuthTokenGenerate();
    setupThemeSelector();
}

void SettingsDialog::reorganizeSettingsTabs()
{
    QVBoxLayout* behaviorLayout = ui->verticalLayout_3;

    auto* securityCard = new QGroupBox();
    securityCard->setObjectName(QStringLiteral("settingsCard"));
    securityCard->setTitle(tr("Encryption && authentication"));
    auto* securityLayout = new QVBoxLayout(securityCard);
    securityLayout->setContentsMargins(20, 22, 20, 20);
    securityLayout->setSpacing(12);

    moveWidget(behaviorLayout, securityLayout, ui->tlsCheckBox);
    moveLayout(behaviorLayout, securityLayout, ui->horizontalLayout_tlsTrust);
    moveWidget(behaviorLayout, securityLayout, ui->authCheckBox);
    moveLayout(behaviorLayout, securityLayout, ui->horizontalLayout_auth);

    auto* reliabilityCard = new QGroupBox();
    reliabilityCard->setObjectName(QStringLiteral("settingsCard"));
    reliabilityCard->setTitle(tr("Transfer engine && recovery"));
    mReliabilityLayout = new QVBoxLayout(reliabilityCard);
    mReliabilityLayout->setContentsMargins(20, 22, 20, 20);
    mReliabilityLayout->setSpacing(12);

    moveLayout(behaviorLayout, mReliabilityLayout, ui->horizontalLayout_5);
    moveLayout(behaviorLayout, mReliabilityLayout, ui->horizontalLayout_maxDownloads);
    moveLayout(behaviorLayout, mReliabilityLayout, ui->horizontalLayout_retryMax);
    moveLayout(behaviorLayout, mReliabilityLayout, ui->horizontalLayout_retryBase);
    moveWidget(behaviorLayout, mReliabilityLayout, ui->journalEnabledCheckBox);
    moveLayout(behaviorLayout, mReliabilityLayout, ui->horizontalLayout_journalRetention);

    auto* storageOptsCard = new QGroupBox();
    storageOptsCard->setObjectName(QStringLiteral("settingsCard"));
    storageOptsCard->setTitle(tr("File handling"));
    auto* storageOptsLayout = new QVBoxLayout(storageOptsCard);
    storageOptsLayout->setContentsMargins(20, 22, 20, 20);
    storageOptsLayout->setSpacing(12);

    moveWidget(behaviorLayout, storageOptsLayout, ui->overwriteCheckBox);
    moveWidget(behaviorLayout, storageOptsLayout, ui->verifyChecksumCheckBox);
    moveWidget(behaviorLayout, storageOptsLayout, ui->resumePartialCheckBox);
    moveLayout(behaviorLayout, storageOptsLayout, ui->horizontalLayout_log);

    ui->verticalLayout_4->removeWidget(ui->groupBox_3);
    ui->groupBox_3->deleteLater();
    ui->verticalLayout_4->removeWidget(ui->groupBox_2);

    ui->groupBox->setTitle(tr("Local identity"));
    ui->appearanceGroupBox->setTitle(tr("Appearance"));
    ui->groupBox_4->setTitle(tr("Discovery"));
    ui->groupBox_5->setTitle(tr("Performance optimization"));
    ui->groupBox_2->setTitle(tr("Download location"));

    ui->tabWidget->insertTab(2, makeTabPage(securityCard), tr("Security"));
    ui->tabWidget->insertTab(3, makeTabPage(reliabilityCard), tr("Reliability"));
    ui->tabWidget->addTab(makeTabPage(ui->groupBox_2, storageOptsCard), tr("Storage"));

    ui->tabWidget->setTabText(1, tr("Network"));
}

void SettingsDialog::styleSettingsCards()
{
    for (QGroupBox* box : findChildren<QGroupBox*>()) {
        box->setProperty("settingsCard", true);
        if (QLayout* layout = box->layout()) {
            layout->setContentsMargins(20, 22, 20, 20);
            layout->setSpacing(12);
        }
        if (box->objectName().isEmpty())
            box->setObjectName(QStringLiteral("settingsCard"));
    }

    ui->label_6->setObjectName(QStringLiteral("settingsFieldLabel"));
    ui->label_5->setObjectName(QStringLiteral("settingsFieldLabel"));
    ui->label_7->setObjectName(QStringLiteral("settingsFieldLabel"));
    ui->label_8->setObjectName(QStringLiteral("settingsFieldLabel"));
    ui->label_parallelStreams->setObjectName(QStringLiteral("settingsFieldLabel"));
}

void SettingsDialog::addNetworkPortHints()
{
    auto* bcHint = new QLabel(tr("Default: 56780"));
    bcHint->setObjectName(QStringLiteral("fieldDefaultHint"));
    auto* bcField = new QWidget();
    auto* bcFieldLayout = new QVBoxLayout(bcField);
    bcFieldLayout->setContentsMargins(0, 0, 0, 0);
    bcFieldLayout->setSpacing(2);
    ui->gridLayout_2->removeWidget(ui->bcPortSpinBox);
    bcFieldLayout->addWidget(ui->bcPortSpinBox);
    bcFieldLayout->addWidget(bcHint);
    ui->gridLayout_2->addWidget(bcField, 0, 1);

    auto* transferHint = new QLabel(tr("Default: 17116"));
    transferHint->setObjectName(QStringLiteral("fieldDefaultHint"));
    auto* transferField = new QWidget();
    auto* transferFieldLayout = new QVBoxLayout(transferField);
    transferFieldLayout->setContentsMargins(0, 0, 0, 0);
    transferFieldLayout->setSpacing(2);
    ui->horizontalLayout_7->removeWidget(ui->transferPortSpinBox);
    transferFieldLayout->addWidget(ui->transferPortSpinBox);
    transferFieldLayout->addWidget(transferHint);
    ui->horizontalLayout_7->addWidget(transferField);
}

void SettingsDialog::addNetworkInfoBanner()
{
    auto* banner = new QLabel(
        tr("Changing port settings may disconnect active transfers. "
           "The listener restarts when you save."));
    banner->setObjectName(QStringLiteral("settingsInfoBanner"));
    banner->setWordWrap(true);
    ui->verticalLayout_7->insertWidget(0, banner);
}

void SettingsDialog::setupParallelStreamsBadge()
{
    mParallelStreamsBadge = new QLabel(this);
    mParallelStreamsBadge->setObjectName(QStringLiteral("parallelStreamsBadge"));
    ui->horizontalLayout_parallelStreams->addWidget(mParallelStreamsBadge);

    connect(ui->parallelStreamsSpinBox, QOverload<int>::of(&QSpinBox::valueChanged),
            this, &SettingsDialog::onParallelStreamsChanged);
    onParallelStreamsChanged(ui->parallelStreamsSpinBox->value());
}

void SettingsDialog::setupAuthTokenGenerate()
{
    auto* generateBtn = new QPushButton(tr("Generate"), this);
    generateBtn->setObjectName(QStringLiteral("generateTokenBtn"));
    ui->horizontalLayout_auth->addWidget(generateBtn);
    connect(generateBtn, &QPushButton::clicked, this, &SettingsDialog::onGenerateTokenClicked);

    const auto syncAuthControls = [this](bool enabled) {
        ui->authTokenLineEdit->setEnabled(enabled);
    };
    syncAuthControls(ui->authCheckBox->isChecked());
    connect(ui->authCheckBox, &QCheckBox::toggled, this, syncAuthControls);
    connect(ui->authCheckBox, &QCheckBox::toggled, generateBtn, &QWidget::setEnabled);
}

void SettingsDialog::setupThemeSelector()
{
    ui->horizontalLayout_appearance->removeWidget(ui->themeComboBox);
    ui->themeComboBox->hide();
    ui->themeComboBox->setParent(this);

    auto* segmentRow = new QWidget(this);
    auto* segmentLayout = new QHBoxLayout(segmentRow);
    segmentLayout->setContentsMargins(0, 0, 0, 0);
    segmentLayout->setSpacing(0);

    mThemeButtonGroup = new QButtonGroup(this);
    mThemeButtonGroup->setExclusive(true);

    const QStringList labels = {tr("System"), tr("Light"), tr("Dark")};
    for (int i = 0; i < labels.size(); ++i) {
        auto* btn = new QPushButton(labels.at(i), segmentRow);
        btn->setCheckable(true);
        btn->setObjectName(QStringLiteral("themeSegmentBtn"));
        btn->setFocusPolicy(Qt::NoFocus);
        mThemeButtonGroup->addButton(btn, i);
        segmentLayout->addWidget(btn);
        connect(btn, &QPushButton::toggled, this, [this, i](bool checked) {
            if (checked)
                ui->themeComboBox->setCurrentIndex(i);
        });
    }

    ui->horizontalLayout_appearance->addWidget(segmentRow);
    ui->horizontalLayout_appearance->addStretch();

    connect(ui->themeComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &SettingsDialog::syncThemeButtons);
    syncThemeButtons();
}

void SettingsDialog::syncThemeButtons()
{
    if (!mThemeButtonGroup)
        return;

    const int idx = ui->themeComboBox->currentIndex();
    if (QAbstractButton* btn = mThemeButtonGroup->button(idx)) {
        QSignalBlocker blocker(mThemeButtonGroup);
        btn->setChecked(true);
    }
}

void SettingsDialog::onGenerateTokenClicked()
{
    ui->authTokenLineEdit->setText(QUuid::createUuid().toString(QUuid::WithoutBraces));
}

void SettingsDialog::onParallelStreamsChanged(int value)
{
    if (!mParallelStreamsBadge)
        return;
    mParallelStreamsBadge->setText(value == 1
            ? tr("1 stream")
            : tr("%1 streams").arg(value));
}

void SettingsDialog::onCancelClicked()
{
    reloadFromSettings();
    emit cancelled();
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

    emit saved();
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

void SettingsDialog::reloadFromSettings()
{
    assign();
}

void SettingsDialog::assign()
{
    Settings* sets = Settings::instance();
    Device me = sets->getMyDevice();

    ui->deviceIdLabel->setText(me.getId());
    ui->ipAddrLabel->setText(me.displayAddress());
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
    syncThemeButtons();
    ui->logPathLabel->setText(AppLog::logFilePath());
    ui->tlsPinnedCountLabel->setText(tr("Pinned peers: %1").arg(TlsHelper::pinnedPeerCount()));
    onParallelStreamsChanged(ui->parallelStreamsSpinBox->value());
}
