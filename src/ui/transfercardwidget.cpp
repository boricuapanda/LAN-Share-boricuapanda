/*
    LANShare - LAN file transfer.
    Copyright (C) 2016 Abdul Aris R. <abdularisrahmanudin10@gmail.com>
*/

#include "transfercardwidget.h"

#include <QFileIconProvider>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QMouseEvent>
#include <QProgressBar>
#include <QStyle>
#include <QVBoxLayout>

#include "model/transferfailure.h"
#include "util.h"

TransferCardWidget::TransferCardWidget(TransferInfo* info, QWidget* parent)
    : QFrame(parent), mInfo(info)
{
    setObjectName(QStringLiteral("transferCard"));
    setFrameShape(QFrame::StyledPanel);
    setCursor(Qt::PointingHandCursor);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(12, 10, 12, 10);
    root->setSpacing(12);

    mIconLabel = new QLabel(this);
    mIconLabel->setFixedSize(36, 36);
    mIconLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(mIconLabel, 0, Qt::AlignTop);

    auto* body = new QVBoxLayout();
    body->setSpacing(4);

    auto* topRow = new QHBoxLayout();
    mFileNameLabel = new QLabel(this);
    mFileNameLabel->setObjectName(QStringLiteral("transferCardFileName"));
    QFont nameFont = mFileNameLabel->font();
    nameFont.setBold(true);
    mFileNameLabel->setFont(nameFont);
    mFileNameLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    topRow->addWidget(mFileNameLabel, 1);

    mBadgeLabel = new QLabel(this);
    mBadgeLabel->setObjectName(QStringLiteral("transferCardBadge"));
    mBadgeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    topRow->addWidget(mBadgeLabel, 0);
    body->addLayout(topRow);

    mPeerLabel = new QLabel(this);
    mPeerLabel->setObjectName(QStringLiteral("transferCardPeer"));
    body->addWidget(mPeerLabel);

    auto* progressRow = new QHBoxLayout();
    mProgress = new QProgressBar(this);
    mProgress->setTextVisible(false);
    mProgress->setFixedHeight(8);
    progressRow->addWidget(mProgress, 1);

    mPercentLabel = new QLabel(this);
    mPercentLabel->setObjectName(QStringLiteral("transferCardPercent"));
    mPercentLabel->setMinimumWidth(36);
    mPercentLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    progressRow->addWidget(mPercentLabel, 0);
    body->addLayout(progressRow);

    mMetaLabel = new QLabel(this);
    mMetaLabel->setObjectName(QStringLiteral("transferCardMeta"));
    body->addWidget(mMetaLabel);

    root->addLayout(body, 1);

    bindInfo();
    refresh();
}

void TransferCardWidget::bindInfo()
{
    if (!mInfo)
        return;

    connect(mInfo, &TransferInfo::progressChanged, this, &TransferCardWidget::refresh);
    connect(mInfo, &TransferInfo::statsChanged, this, &TransferCardWidget::refresh);
    connect(mInfo, &TransferInfo::stateChanged, this, &TransferCardWidget::refresh);
    connect(mInfo, &TransferInfo::fileOpened, this, &TransferCardWidget::refresh);
}

void TransferCardWidget::setSelected(bool selected)
{
    mSelected = selected;
    setProperty("selected", selected);
    style()->unpolish(this);
    style()->polish(this);
    update();
}

void TransferCardWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        emit clicked();
    QFrame::mousePressEvent(event);
}

void TransferCardWidget::mouseDoubleClickEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton)
        emit doubleClicked();
    QFrame::mouseDoubleClickEvent(event);
}

QString TransferCardWidget::peerText(const TransferInfo* info)
{
    const Device peer = info->getPeer();
    const QString peerName = peer.getName();
    const QString peerIp = peer.displayAddress();
    if (info->getTransferType() == TransferType::Upload) {
        if (!peerName.isEmpty() && peerName != peerIp)
            return tr("To %1 (%2)").arg(peerName, peerIp);
        return tr("To %1").arg(peerIp);
    }

    if (!peerName.isEmpty() && peerName != peerIp)
        return tr("From %1 (%2)").arg(peerName, peerIp);
    return tr("From %1").arg(peerIp);
}

QString TransferCardWidget::stateBadgeText(TransferState state, TransferFailureReason reason)
{
    switch (state) {
    case TransferState::Queued:
        return tr("QUEUED");
    case TransferState::Waiting:
        return tr("WAITING");
    case TransferState::Paused:
        return tr("PAUSED");
    case TransferState::Transfering:
        return QString();
    case TransferState::Finish:
        return tr("DONE");
    case TransferState::Failed:
        if (reason != TransferFailureReason::None)
            return transferFailureReasonName(reason);
        return tr("FAILED");
    case TransferState::Cancelled:
        return tr("CANCELLED");
    case TransferState::Disconnected:
        return tr("DISCONNECTED");
    default:
        return QString();
    }
}

void TransferCardWidget::refresh()
{
    if (!mInfo)
        return;

    const QString path = mInfo->getFilePath();
    mFileNameLabel->setText(QFileInfo(path).fileName().isEmpty() ? path : QFileInfo(path).fileName());
    mFileNameLabel->setToolTip(path);
    mPeerLabel->setText(peerText(mInfo));
    mPeerLabel->setToolTip(mPeerLabel->text());

    const QFileInfo fileInfo(path);
    QIcon icon;
    if (fileInfo.exists()) {
        QFileIconProvider provider;
        icon = provider.icon(fileInfo);
    }
    if (icon.isNull())
        icon = style()->standardIcon(QStyle::SP_FileIcon);
    mIconLabel->setPixmap(icon.pixmap(32, 32));

    const TransferState state = mInfo->getState();
    const QString badge = stateBadgeText(state, mInfo->getFailureReason());
    mBadgeLabel->setText(badge);
    mBadgeLabel->setVisible(!badge.isEmpty());

    const bool inactive = state == TransferState::Queued || state == TransferState::Cancelled
            || state == TransferState::Failed || state == TransferState::Finish;
    mProgress->setEnabled(!inactive || state == TransferState::Transfering);
    mProgress->setValue(mInfo->getProgress());
    mPercentLabel->setText(state == TransferState::Transfering || state == TransferState::Finish
                                   ? QStringLiteral("%1%").arg(mInfo->getProgress())
                                   : QString());

    const qint64 total = mInfo->getDataSize();
    const qint64 done = mInfo->getBytesTransferred();

    if (state == TransferState::Queued) {
        mMetaLabel->setText(tr("Waiting in queue..."));
        mProgress->setValue(0);
        mPercentLabel->clear();
    } else if (state == TransferState::Failed) {
        mMetaLabel->setText(mBadgeLabel->text());
    } else if (state == TransferState::Finish) {
        mMetaLabel->setText(tr("Completed • %1").arg(Util::sizeToString(total)));
    } else if (state == TransferState::Transfering || state == TransferState::Paused
               || state == TransferState::Waiting) {
        const QString speed = mInfo->getSpeedText();
        const QString eta = mInfo->getEtaText();
        mMetaLabel->setText(QStringLiteral("%1 • %2 / %3  %4")
                                .arg(speed, Util::sizeToString(done), Util::sizeToString(total), eta)
                                .trimmed());
    } else {
        mMetaLabel->setText(Util::sizeToString(total));
    }

    setProperty("transferState", static_cast<int>(state));
    style()->unpolish(this);
    style()->polish(this);
}
