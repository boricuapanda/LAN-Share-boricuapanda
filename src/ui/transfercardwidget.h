/*
    LANShare - LAN file transfer.
    Copyright (C) 2016 Abdul Aris R. <abdularisrahmanudin10@gmail.com>
*/

#pragma once

#include <QFrame>

#include "model/transferinfo.h"

class QLabel;
class QProgressBar;
class QPropertyAnimation;

class TransferCardWidget : public QFrame
{
    Q_OBJECT

public:
    explicit TransferCardWidget(TransferInfo* info, QWidget* parent = nullptr);

    TransferInfo* transferInfo() const { return mInfo; }
    void setSelected(bool selected);
    bool isSelected() const { return mSelected; }

signals:
    void clicked();
    void doubleClicked();

protected:
    void mousePressEvent(QMouseEvent* event) override;
    void mouseDoubleClickEvent(QMouseEvent* event) override;

private:
    void bindInfo();
    void refresh();
    void setProgressValue(int value, bool animated);
    static QString peerText(const TransferInfo* info);
    static QString stateBadgeText(TransferState state, TransferFailureReason reason);

    TransferInfo* mInfo;
    QLabel* mIconLabel{nullptr};
    QLabel* mFileNameLabel{nullptr};
    QLabel* mPeerLabel{nullptr};
    QLabel* mBadgeLabel{nullptr};
    QLabel* mPercentLabel{nullptr};
    QLabel* mMetaLabel{nullptr};
    QProgressBar* mProgress{nullptr};
    QPropertyAnimation* mProgressAnimation{nullptr};
    bool mSelected{false};
};
