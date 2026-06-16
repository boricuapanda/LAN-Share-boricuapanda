#include "tlstrustdialog.h"

#include <QApplication>
#include <QClipboard>
#include <QFile>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QVBoxLayout>

#include "transfer/tlshelper.h"

TlsTrustDialog::TlsTrustDialog(QWidget* parent)
    : QDialog(parent),
      mPeerList(new QListWidget(this)),
      mFingerprintLabel(new QLabel(this))
{
    setWindowTitle(tr("TLS Trust Inspector"));
    resize(600, 380);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new QLabel(tr("Pinned peers"), this));
    layout->addWidget(mPeerList);

    mFingerprintLabel->setWordWrap(true);
    mFingerprintLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(new QLabel(tr("Fingerprint (SHA-256)"), this));
    layout->addWidget(mFingerprintLabel);

    auto* buttonRow = new QHBoxLayout();
    auto* copyBtn = new QPushButton(tr("Copy Fingerprint"), this);
    auto* exportBtn = new QPushButton(tr("Export..."), this);
    auto* importBtn = new QPushButton(tr("Import..."), this);
    auto* removeBtn = new QPushButton(tr("Remove Selected"), this);
    auto* clearAllBtn = new QPushButton(tr("Clear All"), this);
    auto* closeBtn = new QPushButton(tr("Close"), this);
    closeBtn->setDefault(true);
    buttonRow->addWidget(copyBtn);
    buttonRow->addWidget(exportBtn);
    buttonRow->addWidget(importBtn);
    buttonRow->addWidget(removeBtn);
    buttonRow->addWidget(clearAllBtn);
    buttonRow->addStretch();
    buttonRow->addWidget(closeBtn);
    layout->addLayout(buttonRow);

    connect(mPeerList, &QListWidget::currentTextChanged, this, &TlsTrustDialog::updateFingerprintPreview);
    connect(copyBtn, &QPushButton::clicked, this, &TlsTrustDialog::copyFingerprint);
    connect(exportBtn, &QPushButton::clicked, this, &TlsTrustDialog::exportTrustStore);
    connect(importBtn, &QPushButton::clicked, this, &TlsTrustDialog::importTrustStore);
    connect(removeBtn, &QPushButton::clicked, this, &TlsTrustDialog::removeSelectedPeer);
    connect(clearAllBtn, &QPushButton::clicked, this, &TlsTrustDialog::clearAllPeers);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    reloadPeers();
}

void TlsTrustDialog::reloadPeers()
{
    mPeerList->clear();
    mPeerList->addItems(TlsHelper::pinnedPeerIds());
    if (mPeerList->count() > 0)
        mPeerList->setCurrentRow(0);
    updateFingerprintPreview();
}

void TlsTrustDialog::updateFingerprintPreview()
{
    const QString peerId = mPeerList->currentItem() ? mPeerList->currentItem()->text() : QString();
    const QString fp = TlsHelper::pinnedFingerprint(peerId);
    mFingerprintLabel->setText(fp.isEmpty() ? tr("(none)") : fp);
}

void TlsTrustDialog::copyFingerprint()
{
    const QString fp = mFingerprintLabel->text().trimmed();
    if (fp.isEmpty() || fp == tr("(none)"))
        return;
    QApplication::clipboard()->setText(fp);
}

void TlsTrustDialog::exportTrustStore()
{
    const QString outputPath = QFileDialog::getSaveFileName(
        this, tr("Export TLS Trust Store"), QString(), tr("JSON files (*.json)"));
    if (outputPath.isEmpty())
        return;

    QJsonObject root;
    for (const QString& peerId : TlsHelper::pinnedPeerIds())
        root.insert(peerId, TlsHelper::pinnedFingerprint(peerId));

    QFile out(outputPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Export failed"), tr("Could not write %1").arg(outputPath));
        return;
    }
    out.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    out.close();
}

void TlsTrustDialog::importTrustStore()
{
    const QString inputPath = QFileDialog::getOpenFileName(
        this, tr("Import TLS Trust Store"), QString(), tr("JSON files (*.json)"));
    if (inputPath.isEmpty())
        return;

    QFile in(inputPath);
    if (!in.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("Import failed"), tr("Could not read %1").arg(inputPath));
        return;
    }
    const QJsonDocument doc = QJsonDocument::fromJson(in.readAll());
    in.close();
    if (!doc.isObject()) {
        QMessageBox::warning(this, tr("Import failed"), tr("Invalid trust store file format."));
        return;
    }

    const QJsonObject obj = doc.object();
    for (auto it = obj.begin(); it != obj.end(); ++it) {
        if (it.value().isString())
            TlsHelper::upsertPinnedPeer(it.key(), it.value().toString());
    }
    reloadPeers();
}

void TlsTrustDialog::removeSelectedPeer()
{
    const QString peerId = mPeerList->currentItem() ? mPeerList->currentItem()->text() : QString();
    if (peerId.isEmpty())
        return;

    TlsHelper::removePinnedPeer(peerId);
    reloadPeers();
}

void TlsTrustDialog::clearAllPeers()
{
    if (TlsHelper::pinnedPeerCount() == 0)
        return;

    const QMessageBox::StandardButton answer =
        QMessageBox::question(this, tr("Clear TLS Trust"), tr("Remove all pinned peer fingerprints?"));
    if (answer != QMessageBox::Yes)
        return;

    TlsHelper::clearPinnedPeers();
    reloadPeers();
}
