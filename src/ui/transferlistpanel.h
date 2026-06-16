/*
    LANShare - LAN file transfer.
    Copyright (C) 2016 Abdul Aris R. <abdularisrahmanudin10@gmail.com>
*/

#pragma once

#include <QWidget>

class QLabel;
class QScrollArea;
class QStackedWidget;
class QVBoxLayout;
class TransferCardWidget;
class TransferInfo;
class TransferTableModel;

class TransferListPanel : public QWidget
{
    Q_OBJECT

public:
    explicit TransferListPanel(TransferTableModel* model,
                               const QString& sectionPrefix,
                               const QString& emptyText,
                               const QString& emptyLabelObjectName = QStringLiteral("senderEmptyLabel"),
                               QWidget* parent = nullptr);

    TransferTableModel* model() const { return mModel; }
    int currentRow() const { return mCurrentRow; }
    void setCurrentRow(int row);
    void setHeaderVisible(bool visible);
    int rowAt(const QPoint& pos) const;
    QWidget* dropTargetWidget() const;

signals:
    void currentRowChanged(int row);
    void activated(int row);
    void contextMenuRequested(const QPoint& globalPos, int row);
    void countChanged(int count);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void rebuildCards();
    void insertCard(int row);
    void removeCard(int row);
    void updateSectionCount();
    void onCardClicked(TransferCardWidget* card);
    void onCardDoubleClicked(TransferCardWidget* card);

    TransferTableModel* mModel;
    QString mSectionPrefix;
    QLabel* mCountLabel{nullptr};
    QStackedWidget* mStack{nullptr};
    QLabel* mEmptyLabel{nullptr};
    QScrollArea* mScrollArea{nullptr};
    QWidget* mCardContainer{nullptr};
    QVBoxLayout* mCardLayout{nullptr};
    QVector<TransferCardWidget*> mCards;
    int mCurrentRow{-1};
};
