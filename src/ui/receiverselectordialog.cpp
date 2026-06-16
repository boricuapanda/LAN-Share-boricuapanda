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

#include <QMessageBox>
#include <QSortFilterProxyModel>

#include "receiverselectordialog.h"
#include "ui_receiverselectordialog.h"

#include "model/devicelistmodel.h"
#include "model/device.h"

namespace {

class DeviceFilterProxyModel : public QSortFilterProxyModel
{
public:
    explicit DeviceFilterProxyModel(QObject* parent = nullptr)
        : QSortFilterProxyModel(parent)
    {
        setFilterCaseSensitivity(Qt::CaseInsensitive);
    }

protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex& /*sourceParent*/) const override
    {
        if (filterRegularExpression().pattern().isEmpty())
            return true;

        const auto* model = static_cast<const DeviceListModel*>(sourceModel());
        if (!model)
            return false;

        const Device dev = model->device(sourceRow);
        const QString haystack = dev.getName() + QLatin1Char(' ')
                + dev.getAddress().toString() + QLatin1Char(' ')
                + dev.getOSName();
        return haystack.contains(filterRegularExpression());
    }
};

} // namespace

ReceiverSelectorDialog::ReceiverSelectorDialog(DeviceListModel* model, QWidget *parent) :
    QDialog(parent),
    ui(new Ui::ReceiverSelectorDialog),
    mModel(model),
    mProxyModel(new DeviceFilterProxyModel(this))
{
    ui->setupUi(this);
    resize(420, 360);

    ui->label->setObjectName(QStringLiteral("dialogTitle"));
    ui->label_multicast->setObjectName(QStringLiteral("hintLabel"));
    ui->emptyLabel->setObjectName(QStringLiteral("receiverEmptyLabel"));
    ui->pushButton->setProperty("primary", true);

    mProxyModel->setSourceModel(mModel);
    ui->listView->setModel(mProxyModel);
    ui->listView->setCurrentIndex(QModelIndex());

    ui->emptyLabel->hide();

    connect(ui->searchLineEdit, &QLineEdit::textChanged,
            this, &ReceiverSelectorDialog::onSearchTextChanged);
    connect(mProxyModel, &QAbstractItemModel::rowsInserted,
            this, &ReceiverSelectorDialog::updateEmptyLabel);
    connect(mProxyModel, &QAbstractItemModel::rowsRemoved,
            this, &ReceiverSelectorDialog::updateEmptyLabel);
    connect(mProxyModel, &QAbstractItemModel::modelReset,
            this, &ReceiverSelectorDialog::updateEmptyLabel);
    connect(mProxyModel, &QAbstractItemModel::layoutChanged,
            this, &ReceiverSelectorDialog::updateEmptyLabel);

    model->refresh();
    updateEmptyLabel();
}

ReceiverSelectorDialog::~ReceiverSelectorDialog()
{
    delete ui;
}

Device ReceiverSelectorDialog::getSelectedDevice() const
{
    const QModelIndex currIndex = ui->listView->currentIndex();
    if (!currIndex.isValid())
        return Device();

    const QModelIndex sourceIndex = mProxyModel->mapToSource(currIndex);
    if (!sourceIndex.isValid())
        return Device();

    return mModel->device(sourceIndex.row());
}

QVector<Device> ReceiverSelectorDialog::getSelectedDevices() const
{
    QVector<Device> devices;
    QItemSelectionModel* selModel = ui->listView->selectionModel();
    if (!selModel)
        return devices;

    const QModelIndexList selected = selModel->selectedIndexes();
    for (const QModelIndex& selectedIndex : selected) {
        if (!selectedIndex.isValid())
            continue;

        const QModelIndex sourceIndex = mProxyModel->mapToSource(selectedIndex);
        if (sourceIndex.isValid())
            devices.push_back(mModel->device(sourceIndex.row()));
    }

    return devices;
}

void ReceiverSelectorDialog::onSendClicked()
{
    if (ui->listView->currentIndex().isValid())
        accept();
    else
        QMessageBox::information(this, tr("Info"), tr("Please select receivers."));
}

void ReceiverSelectorDialog::onRefreshClicked()
{
    mModel->refresh();
    updateEmptyLabel();
}

void ReceiverSelectorDialog::onSearchTextChanged(const QString& text)
{
    mProxyModel->setFilterFixedString(text);
    updateEmptyLabel();
}

void ReceiverSelectorDialog::updateEmptyLabel()
{
    const bool showEmpty = mProxyModel->rowCount() == 0;
    ui->emptyLabel->setVisible(showEmpty);
    ui->listView->setVisible(!showEmpty);
}
