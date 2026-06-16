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

#include "transferprogresswidget.h"

#include <QEvent>
#include <QFont>
#include <QLabel>
#include <QProgressBar>
#include <QVBoxLayout>

#include "model/transferfailure.h"
#include "model/transferinfo.h"

TransferProgressWidget::TransferProgressWidget(TransferInfo* info, QWidget* parent)
    : QWidget(parent),
      mInfo(info)
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(2, 2, 2, 2);
    layout->setSpacing(0);

    mProgress = new QProgressBar(this);
    mStats = new QLabel(this);
    mStats->setFont(QFont(mStats->font().family(), mStats->font().pointSize() - 1));

    layout->addWidget(mProgress);
    layout->addWidget(mStats);

    updateStatsPalette();

    connect(info, &TransferInfo::progressChanged, mProgress, &QProgressBar::setValue);
    connect(info, &TransferInfo::statsChanged, this, &TransferProgressWidget::updateDisplay);
    connect(info, &TransferInfo::progressChanged, this, &TransferProgressWidget::updateDisplay);
    connect(info, &TransferInfo::stateChanged, this, &TransferProgressWidget::updateDisplay);
    updateDisplay();
}

void TransferProgressWidget::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange)
        updateStatsPalette();
}

void TransferProgressWidget::updateStatsPalette()
{
    QPalette pal = mStats->palette();
    pal.setColor(QPalette::WindowText, palette().color(QPalette::PlaceholderText));
    mStats->setPalette(pal);
}

void TransferProgressWidget::updateDisplay()
{
    const TransferState state = mInfo->getState();
    if (state == TransferState::Queued) {
        mProgress->setValue(0);
        mProgress->setEnabled(false);
        mStats->setText(tr("Waiting in queue..."));
        return;
    }

    mProgress->setEnabled(true);

    if (state == TransferState::Failed) {
        const TransferFailureReason reason = mInfo->getFailureReason();
        if (reason != TransferFailureReason::None)
            mStats->setText(transferFailureReasonName(reason));
        else
            mStats->setText(tr("Failed"));
        return;
    }

    mStats->setText(mInfo->getSpeedText() + QStringLiteral("  ") + mInfo->getEtaText());
}
