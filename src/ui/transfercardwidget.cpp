/*
    LANShare - LAN file transfer.
    Copyright (C) 2016 Abdul Aris R. <abdularisrahmanudin10@gmail.com>
*/

#include "transfercardwidget.h"

#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QStyle>
#include <QVBoxLayout>

#include "model/transferfailure.h"
#include "util.h"

namespace {

QPixmap transferFilePixmap(TransferType type, TransferState state)
{
    const bool isUpload = type == TransferType::Upload;
    const bool inactive = state == TransferState::Cancelled || state == TransferState::Failed
            || state == TransferState::Finish;
    const QColor accent = inactive
            ? QColor(QStringLiteral("#9aa8b5"))
            : (isUpload ? QColor(QStringLiteral("#0c8fb8")) : QColor(QStringLiteral("#21a67a")));

    QPixmap pixmap(28, 28);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    QRectF page(5.5, 3.5, 15.5, 21.0);
    painter.setPen(QPen(QColor(QStringLiteral("#a9c6d8")), 1.1));
    painter.setBrush(QColor(QStringLiteral("#fbfdff")));
    painter.drawRoundedRect(page, 2.2, 2.2);

    QPolygonF fold;
    fold << QPointF(16.0, 3.5) << QPointF(21.0, 8.5) << QPointF(16.0, 8.5);
    painter.setPen(QPen(QColor(QStringLiteral("#c7ddea")), 1.0));
    painter.setBrush(QColor(QStringLiteral("#eef7fb")));
    painter.drawPolygon(fold);

    painter.setPen(QPen(accent, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (isUpload) {
        painter.drawLine(QPointF(15.5, 18.0), QPointF(23.0, 10.5));
        painter.drawLine(QPointF(23.0, 10.5), QPointF(23.0, 16.0));
        painter.drawLine(QPointF(23.0, 10.5), QPointF(17.5, 10.5));
    } else {
        painter.drawLine(QPointF(23.0, 10.5), QPointF(15.5, 18.0));
        painter.drawLine(QPointF(15.5, 18.0), QPointF(21.0, 18.0));
        painter.drawLine(QPointF(15.5, 18.0), QPointF(15.5, 12.5));
    }

    painter.setPen(QPen(QColor(QStringLiteral("#d7e9f2")), 1.2, Qt::SolidLine, Qt::RoundCap));
    painter.drawLine(QPointF(8.5, 13.0), QPointF(13.5, 13.0));
    painter.drawLine(QPointF(8.5, 16.5), QPointF(12.0, 16.5));

    return pixmap;
}

} // namespace

TransferCardWidget::TransferCardWidget(TransferInfo* info, QWidget* parent)
    : QFrame(parent), mInfo(info)
{
    setObjectName(QStringLiteral("transferCard"));
    setFrameShape(QFrame::StyledPanel);
    setCursor(Qt::PointingHandCursor);

    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(8, 6, 8, 6);
    root->setSpacing(8);

    mIconLabel = new QLabel(this);
    mIconLabel->setObjectName(QStringLiteral("transferCardIcon"));
    mIconLabel->setFixedSize(26, 26);
    mIconLabel->setAlignment(Qt::AlignCenter);
    root->addWidget(mIconLabel, 0, Qt::AlignTop);

    auto* body = new QVBoxLayout();
    body->setSpacing(2);

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
    mProgress->setObjectName(QStringLiteral("transferProgress"));
    mProgress->setTextVisible(false);
    mProgress->setFixedHeight(6);
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

    mProgressAnimation = new QPropertyAnimation(mProgress, "value", this);
    mProgressAnimation->setDuration(260);
    mProgressAnimation->setEasingCurve(QEasingCurve::OutCubic);

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

    const TransferState state = mInfo->getState();
    mIconLabel->setPixmap(transferFilePixmap(mInfo->getTransferType(), state));

    const QString badge = stateBadgeText(state, mInfo->getFailureReason());
    mBadgeLabel->setText(badge);
    mBadgeLabel->setVisible(!badge.isEmpty());

    const bool inactive = state == TransferState::Queued || state == TransferState::Cancelled
            || state == TransferState::Failed || state == TransferState::Finish;
    mProgress->setEnabled(!inactive || state == TransferState::Transfering);
    setProgressValue(mInfo->getProgress(), state == TransferState::Transfering);
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
        mMetaLabel->setText(tr("Completed - %1").arg(Util::sizeToString(total)));
    } else if (state == TransferState::Transfering || state == TransferState::Paused
               || state == TransferState::Waiting) {
        const QString speed = mInfo->getSpeedText();
        const QString eta = mInfo->getEtaText();
        mMetaLabel->setText(QStringLiteral("%1 - %2 / %3  %4")
                                .arg(speed, Util::sizeToString(done), Util::sizeToString(total), eta)
                                .trimmed());
    } else {
        mMetaLabel->setText(Util::sizeToString(total));
    }

    setProperty("transferState", static_cast<int>(state));
    style()->unpolish(this);
    style()->polish(this);
}

void TransferCardWidget::setProgressValue(int value, bool animated)
{
    value = qBound(0, value, 100);
    if (!animated || qAbs(mProgress->value() - value) <= 1) {
        if (mProgressAnimation)
            mProgressAnimation->stop();
        mProgress->setValue(value);
        return;
    }

    mProgressAnimation->stop();
    mProgressAnimation->setStartValue(mProgress->value());
    mProgressAnimation->setEndValue(value);
    mProgressAnimation->start();
}
