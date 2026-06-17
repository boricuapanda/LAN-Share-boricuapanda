/*
    LANShare - LAN file transfer.
    Copyright (C) 2016 Abdul Aris R. <abdularisrahmanudin10@gmail.com>
*/

#include "transferlistpanel.h"

#include <QAbstractItemModel>
#include <QContextMenuEvent>
#include <QLabel>
#include <QScrollArea>
#include <QScrollBar>
#include <QStackedWidget>
#include <QVBoxLayout>

#include "model/transfertablemodel.h"
#include "transfercardwidget.h"

TransferListPanel::TransferListPanel(TransferTableModel* model,
                                     const QString& sectionPrefix,
                                     const QString& emptyText,
                                     const QString& emptyLabelObjectName,
                                     QWidget* parent)
    : QWidget(parent), mModel(model), mSectionPrefix(sectionPrefix)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(6);

    mCountLabel = new QLabel(this);
    mCountLabel->setObjectName(QStringLiteral("sectionHeader"));
    layout->addWidget(mCountLabel);

    mStack = new QStackedWidget(this);
    layout->addWidget(mStack, 1);

    mScrollArea = new QScrollArea(mStack);
    mScrollArea->setWidgetResizable(true);
    mScrollArea->setFrameShape(QFrame::NoFrame);
    mScrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    mCardContainer = new QWidget(mScrollArea);
    mCardLayout = new QVBoxLayout(mCardContainer);
    mCardLayout->setContentsMargins(10, 0, 10, 8);
    mCardLayout->setSpacing(5);
    mCardLayout->addStretch();
    mScrollArea->setWidget(mCardContainer);
    mStack->addWidget(mScrollArea);

    mEmptyLabel = new QLabel(emptyText, mStack);
    mEmptyLabel->setObjectName(emptyLabelObjectName);
    mEmptyLabel->setAlignment(Qt::AlignCenter);
    mEmptyLabel->setWordWrap(true);
    mStack->addWidget(mEmptyLabel);

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint& pos) {
        const int row = rowAt(mapTo(mCardContainer, pos));
        emit contextMenuRequested(mapToGlobal(pos), row);
    });

    connect(mModel, &QAbstractItemModel::rowsInserted, this, [this](const QModelIndex&, int first, int last) {
        for (int row = first; row <= last; ++row)
            insertCard(row);
        updateSectionCount();
    });
    connect(mModel, &QAbstractItemModel::rowsRemoved, this, [this](const QModelIndex&, int first, int last) {
        for (int row = last; row >= first; --row)
            removeCard(row);
        if (mCurrentRow >= mModel->rowCount())
            setCurrentRow(mModel->rowCount() - 1);
        updateSectionCount();
    });
    connect(mModel, &QAbstractItemModel::modelReset, this, [this]() {
        rebuildCards();
        updateSectionCount();
    });

    rebuildCards();
    updateSectionCount();
}

void TransferListPanel::rebuildCards()
{
    while (!mCards.isEmpty())
        removeCard(0);

    for (int row = 0; row < mModel->rowCount(); ++row)
        insertCard(row);
}

void TransferListPanel::insertCard(int row)
{
    TransferInfo* info = mModel->getTransferInfo(row);
    if (!info)
        return;

    auto* card = new TransferCardWidget(info, mCardContainer);
    connect(card, &TransferCardWidget::clicked, this, [this, card]() { onCardClicked(card); });
    connect(card, &TransferCardWidget::doubleClicked, this, [this, card]() { onCardDoubleClicked(card); });
    card->installEventFilter(this);

    const int layoutIndex = qMin(row, mCardLayout->count() - 1);
    mCardLayout->insertWidget(layoutIndex, card);
    mCards.insert(row, card);

    if (mCurrentRow >= row)
        ++mCurrentRow;

    if (row == 0)
        mScrollArea->verticalScrollBar()->setValue(0);
}

void TransferListPanel::removeCard(int row)
{
    if (row < 0 || row >= mCards.size())
        return;

    const int previousCurrentRow = mCurrentRow;
    TransferCardWidget* card = mCards.takeAt(row);
    mCardLayout->removeWidget(card);
    const bool removedSelectedCard = card->isSelected() && mCurrentRow == row;
    if (removedSelectedCard)
        mCurrentRow = -1;
    else if (mCurrentRow > row)
        --mCurrentRow;

    if (mCurrentRow >= 0 && mCurrentRow < mCards.size())
        mCards.at(mCurrentRow)->setSelected(true);

    card->deleteLater();

    if (mCurrentRow != previousCurrentRow)
        emit currentRowChanged(mCurrentRow);
}

void TransferListPanel::setCurrentRow(int row)
{
    if (row == mCurrentRow)
        return;

    if (mCurrentRow >= 0 && mCurrentRow < mCards.size())
        mCards.at(mCurrentRow)->setSelected(false);

    mCurrentRow = row;

    if (mCurrentRow >= 0 && mCurrentRow < mCards.size())
        mCards.at(mCurrentRow)->setSelected(true);

    emit currentRowChanged(mCurrentRow);
}

void TransferListPanel::setHeaderVisible(bool visible)
{
    mCountLabel->setVisible(visible);
}

void TransferListPanel::onCardClicked(TransferCardWidget* card)
{
    const int row = mCards.indexOf(card);
    if (row >= 0)
        setCurrentRow(row);
}

void TransferListPanel::onCardDoubleClicked(TransferCardWidget* card)
{
    const int row = mCards.indexOf(card);
    if (row >= 0)
        emit activated(row);
}

int TransferListPanel::rowAt(const QPoint& pos) const
{
    QWidget* child = mCardContainer->childAt(pos);
    while (child && child != mCardContainer) {
        auto* card = qobject_cast<TransferCardWidget*>(child);
        if (card)
            return mCards.indexOf(card);
        child = child->parentWidget();
    }
    return -1;
}

QWidget* TransferListPanel::dropTargetWidget() const
{
    return mStack;
}

void TransferListPanel::updateSectionCount()
{
    const int count = mModel->rowCount();
    mCountLabel->setText(tr("%1 (%2 total)").arg(mSectionPrefix).arg(count));
    mStack->setCurrentIndex(count == 0 ? 1 : 0);
    emit countChanged(count);
}

bool TransferListPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() == QEvent::ContextMenu) {
        auto* card = qobject_cast<TransferCardWidget*>(watched);
        if (card) {
            const int row = mCards.indexOf(card);
            if (row >= 0) {
                setCurrentRow(row);
                auto* ctx = static_cast<QContextMenuEvent*>(event);
                emit contextMenuRequested(ctx->globalPos(), row);
                return true;
            }
        }
    }
    return QWidget::eventFilter(watched, event);
}
