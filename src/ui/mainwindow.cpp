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

#include <QDesktopServices>
#include <QFileDialog>
#include <QMessageBox>
#include <QMenu>
#include <QToolButton>
#include <QCloseEvent>
#include <QtDebug>
#include <QListView>
#include <QTreeView>
#include <QLabel>
#include <QStackedWidget>
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QHeaderView>

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "receiverselectordialog.h"
#include "settingsdialog.h"
#include "settings.h"
#include "aboutdialog.h"
#include "logviewerdialog.h"
#include "transferprogresswidget.h"
#include "ui/uitheme.h"
#include "util.h"
#include "log.h"
#include "transfer/sender.h"
#include "transfer/receiver.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setupActions();
    setupToolbar();
    setupSystrayIcon();
    setWindowTitle(PROGRAM_NAME);

    ui->label->setObjectName(QStringLiteral("sectionHeader"));
    ui->label_2->setObjectName(QStringLiteral("sectionHeader"));

    mBroadcaster = new DeviceBroadcaster(this);
    mBroadcaster->start();
    mSenderModel = new TransferTableModel(this);
    mReceiverModel = new TransferTableModel(this);
    mDeviceModel = new DeviceListModel(mBroadcaster, this);
    mTransServer = new TransferServer(mDeviceModel, this);
    mTransServer->listen();

//    mSenderModel->setHeaderData((int) TransferTableModel::Column::Peer, Qt::Horizontal, tr("Receiver"));
//    mReceiverModel->setHeaderData((int) TransferTableModel::Column::Peer, Qt::Horizontal, tr("Sender"));

    ui->senderTableView->setModel(mSenderModel);
    ui->receiverTableView->setModel(mReceiverModel);

    setupTableStacks();
    setupTablePolish();
    setupAccessibility();

    setAcceptDrops(true);
    if (mSenderStack) {
        mSenderStack->setAcceptDrops(true);
        mSenderStack->installEventFilter(this);
    }

    ui->senderTableView->setColumnWidth((int)TransferTableModel::Column::FileName, 340);
    ui->senderTableView->setColumnWidth((int)TransferTableModel::Column::Progress, 200);

    ui->receiverTableView->setColumnWidth((int)TransferTableModel::Column::FileName, 340);
    ui->receiverTableView->setColumnWidth((int)TransferTableModel::Column::Progress, 200);

    mStatusLabel = new QLabel(this);
    statusBar()->addPermanentWidget(mStatusLabel);

    connectTransferModelSignals();
    connectSignals();
    updateEmptyStates();
    updateStatusBar();
}

MainWindow::~MainWindow()
{
    delete ui;
}

/*
 * Sebelum ditutup, check apakah masih terdapat proses trasfer
 * yg berlangsung, (Sending atau Receiving)
 */
void MainWindow::closeEvent(QCloseEvent *event)
{
    if (mSystrayIcon && mSystrayIcon->isVisible() && !mForceQuit) {
        setMainWindowVisibility(false);
        event->ignore();
        return;
    }

    auto checkTransferState = [](Transfer* t) {
        if (!t)
            return false;
        TransferState state = t->getTransferInfo()->getState();
        return state == TransferState::Paused ||
                state == TransferState::Transfering ||
                state == TransferState::Waiting ||
                state == TransferState::Queued;
    };

    auto checkTransferModel = [&](TransferTableModel* model) {
        int count = model->rowCount();
        for (int i = 0; i < count; i++) {
            Transfer* t = model->getTransfer(i);
            if (checkTransferState(t)) {
                return true;
            }
        }

        return false;
    };

    bool needToConfirm = checkTransferModel(mSenderModel);
    if (!needToConfirm)
        needToConfirm = checkTransferModel(mReceiverModel);

    if (needToConfirm) {
        QMessageBox::StandardButton ret =
                QMessageBox::question(this, tr("Confirm close"),
                                      tr("You are about to close & abort all transfers. Do you want to continue?"));
        if (ret == QMessageBox::No) {
            event->ignore();
            mForceQuit = false;
            return;
        }
    }

    event->accept();
    qApp->quit();
}

void MainWindow::setMainWindowVisibility(bool visible)
{
    if (visible) {
        showNormal();
        setWindowState(Qt::WindowNoState);
        qApp->processEvents();
        setWindowState(Qt::WindowActive);
        qApp->processEvents();
        qApp->setActiveWindow(this);
        qApp->processEvents();
    }
    else {
        hide();
    }
}

void MainWindow::connectSignals()
{
    connect(mTransServer, &TransferServer::newReceiverAdded, this, &MainWindow::onNewReceiverAdded);

    QItemSelectionModel* senderSel = ui->senderTableView->selectionModel();
    connect(senderSel, &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onSenderTableSelectionChanged);

    QItemSelectionModel* receiverSel = ui->receiverTableView->selectionModel();
    connect(receiverSel, &QItemSelectionModel::selectionChanged,
            this, &MainWindow::onReceiverTableSelectionChanged);
}

void MainWindow::sendFile(const QString& folderName, const QString &filePath, const Device &receiver)
{
    Sender* sender = new Sender(receiver, folderName, filePath, this);
    mSenderModel->insertTransfer(sender);
    QModelIndex progressIdx = mSenderModel->index(0, (int)TransferTableModel::Column::Progress);

    QWidget* progressWidget = new TransferProgressWidget(sender->getTransferInfo());
    ui->senderTableView->setIndexWidget(progressIdx, progressWidget);
    ui->senderTableView->scrollToTop();

    TransferInfo* info = sender->getTransferInfo();
    info->setFilePath(filePath);
    connectSenderQueueSignals(info);

    const int maxTransfers = Settings::instance()->getMaxConcurrentTransfers();
    if (maxTransfers > 0 && activeSenderCount() >= maxTransfers) {
        info->setState(TransferState::Queued);
        AppLog::write("transfer", QString("Upload queued: %1").arg(filePath));
    } else if (!sender->start()) {
        AppLog::write("transfer", QString("Upload failed to start: %1").arg(filePath));
    }

    scheduleUpdateStatusBar();
}

int MainWindow::activeSenderCount() const
{
    int count = 0;
    const int rows = mSenderModel->rowCount();
    for (int i = 0; i < rows; ++i) {
        const TransferState state = mSenderModel->getTransferInfo(i)->getState();
        if (state == TransferState::Waiting ||
                state == TransferState::Transfering ||
                state == TransferState::Paused) {
            ++count;
        }
    }
    return count;
}

void MainWindow::connectSenderQueueSignals(TransferInfo* info)
{
    connect(info, &TransferInfo::done, this, &MainWindow::processSendQueue);
    connect(info, &TransferInfo::errorOcurred, this, &MainWindow::processSendQueue);
    connect(info, &TransferInfo::stateChanged, this, [this](TransferState state) {
        if (state == TransferState::Cancelled || state == TransferState::Disconnected)
            processSendQueue();
        scheduleUpdateStatusBar();
    });
}

void MainWindow::processSendQueue()
{
    const int maxTransfers = Settings::instance()->getMaxConcurrentTransfers();
    if (maxTransfers == 0)
        return;

    int active = activeSenderCount();
    const int rows = mSenderModel->rowCount();
    for (int i = rows - 1; i >= 0 && active < maxTransfers; --i) {
        TransferInfo* info = mSenderModel->getTransferInfo(i);
        if (info->getState() != TransferState::Queued)
            continue;

        auto* sender = static_cast<Sender*>(info->getOwner());
        if (!sender)
            continue;

        if (sender->start()) {
            ++active;
            AppLog::write("transfer", QString("Upload dequeued: %1").arg(info->getFilePath()));
        }
    }
}

void MainWindow::selectReceiversAndSendTheFiles(QVector<QPair<QString, QString> > dirNameAndFullPath)
{
    ReceiverSelectorDialog dialog(mDeviceModel);
    if (dialog.exec() == QDialog::Accepted) {
        QVector<Device> receivers = dialog.getSelectedDevices();
        for (const Device& receiver : receivers) {
            if (receiver.isValid()) {

                /*
                 * Memastikan bahwa device/kompuer ini terdaftar di penerima
                 * Just to make sure.
                 */
                mBroadcaster->sendBroadcast();
                for (const auto& p : dirNameAndFullPath) {
                    sendFile(p.first, p.second, receiver);
                }

            }
        }
    }
}

void MainWindow::onShowMainWindowTriggered()
{
    setMainWindowVisibility(true);
}

bool MainWindow::mimeHasLocalUrls(const QMimeData* mimeData)
{
    if (!mimeData || !mimeData->hasUrls())
        return false;

    for (const QUrl& url : mimeData->urls()) {
        if (url.isLocalFile() && QFileInfo(url.toLocalFile()).exists())
            return true;
    }

    return false;
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event)
{
    if (mimeHasLocalUrls(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void MainWindow::dragMoveEvent(QDragMoveEvent* event)
{
    if (mimeHasLocalUrls(event->mimeData()))
        event->acceptProposedAction();
    else
        event->ignore();
}

void MainWindow::dropEvent(QDropEvent* event)
{
    const QMimeData* mimeData = event->mimeData();
    if (!mimeHasLocalUrls(mimeData)) {
        event->ignore();
        return;
    }

    QVector<QPair<QString, QString>> pairs;
    for (const QUrl& url : mimeData->urls()) {
        if (!url.isLocalFile())
            continue;

        const QString path = url.toLocalFile();
        const QFileInfo info(path);
        if (!info.exists())
            continue;

        if (info.isDir()) {
            QDir dir(path);
            pairs.append(Util::getInnerDirNameAndFullFilePath(dir, dir.dirName()));
        } else if (info.isFile()) {
            pairs.push_back(qMakePair(QString(), path));
        }
    }

    if (pairs.isEmpty()) {
        event->ignore();
        return;
    }

    event->acceptProposedAction();
    selectReceiversAndSendTheFiles(pairs);
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == mSenderStack) {
        switch (event->type()) {
        case QEvent::DragEnter:
            dragEnterEvent(static_cast<QDragEnterEvent*>(event));
            return true;
        case QEvent::DragMove:
            dragMoveEvent(static_cast<QDragMoveEvent*>(event));
            return true;
        case QEvent::Drop:
            dropEvent(static_cast<QDropEvent*>(event));
            return true;
        default:
            break;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onSendFilesActionTriggered()
{
    const QStringList fileNames = Util::selectOpenFileNames(this, tr("Select files"));
    if (fileNames.isEmpty())
        return;

    QVector<QPair<QString, QString> > pairs;
    for (const auto& fName : fileNames)
        pairs.push_back( QPair<QString, QString>("", fName) );

    selectReceiversAndSendTheFiles(pairs);
}

void MainWindow::onSendFolderActionTriggered()
{
    const QStringList dirs = Util::selectExistingDirectories(this, tr("Select folders"));
    if (dirs.isEmpty())
        return;

    /*
     * Iterate through all selected folders
     */
    QVector< QPair<QString, QString> > pairs;
    for (const auto& dirName : dirs) {

        QDir dir(dirName);
        QVector< QPair<QString, QString> > ps =
                Util::getInnerDirNameAndFullFilePath(dir, dir.dirName());
        pairs.append(ps);
    }

    selectReceiversAndSendTheFiles(pairs);
}

void MainWindow::onSettingsActionTriggered()
{
    SettingsDialog dialog;
    dialog.exec();
}

void MainWindow::onAboutActionTriggered()
{
    AboutDialog dialog;
    dialog.exec();
}

void MainWindow::onViewLogActionTriggered()
{
    LogViewerDialog dialog(this);
    dialog.exec();
}

void MainWindow::onNewReceiverAdded(Receiver *rec)
{
    QWidget* progressWidget = new TransferProgressWidget(rec->getTransferInfo());
    mReceiverModel->insertTransfer(rec);
    QModelIndex progressIdx = mReceiverModel->index(0, (int)TransferTableModel::Column::Progress);

    ui->receiverTableView->setIndexWidget(progressIdx, progressWidget);
    ui->receiverTableView->scrollToTop();

    scheduleUpdateStatusBar();
}

void MainWindow::onSenderTableSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    if (selected.size() > 0) {

        QModelIndex first = selected.indexes().first();
        if (first.isValid()) {
            TransferInfo* ti = mSenderModel->getTransferInfo(first.row());
            ui->resumeSenderBtn->setEnabled(ti->canResume());
            ui->pauseSenderBtn->setEnabled(ti->canPause());
            ui->cancelSenderBtn->setEnabled(ti->canCancel());

            connect(ti, &TransferInfo::stateChanged, this, &MainWindow::onSelectedSenderStateChanged);
        }

    }

    if (deselected.size() > 0) {

        QModelIndex first = deselected.indexes().first();
        if (first.isValid()) {
            TransferInfo* ti = mSenderModel->getTransferInfo(first.row());
            disconnect(ti, &TransferInfo::stateChanged, this, &MainWindow::onSelectedSenderStateChanged);
        }

    }
}

void MainWindow::onSenderTableDoubleClicked(const QModelIndex& index)
{
    Q_UNUSED(index);
    openSenderFileInCurrentIndex();
}

void MainWindow::onSenderClearClicked()
{
    mSenderModel->clearCompleted();
    updateEmptyStates();
    scheduleUpdateStatusBar();
}

void MainWindow::onSenderCancelClicked()
{
    QModelIndex currIndex = ui->senderTableView->currentIndex();
    if (currIndex.isValid()) {
        Transfer* sender = mSenderModel->getTransfer(currIndex.row());
        sender->cancel();
    }
}

void MainWindow::onSenderPauseClicked()
{
    QModelIndex currIndex = ui->senderTableView->currentIndex();
    if (currIndex.isValid()) {
        Transfer* sender = mSenderModel->getTransfer(currIndex.row());
        sender->pause();
    }
}

void MainWindow::onSenderResumeClicked()
{
    QModelIndex currIndex = ui->senderTableView->currentIndex();
    if (currIndex.isValid()) {
        Transfer* sender = mSenderModel->getTransfer(currIndex.row());
        sender->resume();
    }
}


void MainWindow::onReceiverTableDoubleClicked(const QModelIndex& index)
{
    if (index.isValid()) {
        TransferInfo* ti = mReceiverModel->getTransferInfo(index.row());
        if (ti && ti->getState() == TransferState::Finish)
            openReceiverFileInCurrentIndex();
    }
}

void MainWindow::onReceiverClearClicked()
{
    mReceiverModel->clearCompleted();
    updateEmptyStates();
    scheduleUpdateStatusBar();
}

void MainWindow::onReceiverTableSelectionChanged(const QItemSelection &selected, const QItemSelection &deselected)
{
    if (selected.size() > 0) {

        QModelIndex first = selected.indexes().first();
        if (first.isValid()) {
            TransferInfo* ti = mReceiverModel->getTransferInfo(first.row());
            ui->resumeReceiverBtn->setEnabled(ti->canResume());
            ui->pauseReceiverBtn->setEnabled(ti->canPause());
            ui->cancelReceiverBtn->setEnabled(ti->canCancel());

            connect(ti, &TransferInfo::stateChanged, this, &MainWindow::onSelectedReceiverStateChanged);
        }

    }

    if (deselected.size() > 0) {

        QModelIndex first = deselected.indexes().first();
        if (first.isValid()) {
            TransferInfo* ti = mReceiverModel->getTransferInfo(first.row());
            disconnect(ti, &TransferInfo::stateChanged, this, &MainWindow::onSelectedReceiverStateChanged);
        }

    }
}

void MainWindow::onReceiverCancelClicked()
{
    QModelIndex currIndex = ui->receiverTableView->currentIndex();
    if (currIndex.isValid()) {
        Transfer* rec = mReceiverModel->getTransfer(currIndex.row());
        rec->cancel();
    }
}

void MainWindow::onReceiverPauseClicked()
{
    QModelIndex currIndex = ui->receiverTableView->currentIndex();
    if (currIndex.isValid()) {
        Transfer* rec = mReceiverModel->getTransfer(currIndex.row());
        rec->pause();
    }
}

void MainWindow::onReceiverResumeClicked()
{
    QModelIndex currIndex = ui->receiverTableView->currentIndex();
    if (currIndex.isValid()) {
        Transfer* rec = mReceiverModel->getTransfer(currIndex.row());
        rec->resume();
    }
}

void MainWindow::onSenderTableContextMenuRequested(const QPoint& pos)
{
    QModelIndex currIndex = ui->senderTableView->indexAt(pos);
    QMenu contextMenu;

    if (currIndex.isValid()) {
        TransferInfo* ti = mSenderModel->getTransferInfo(currIndex.row());
        TransferState state = ti->getState();
        bool enableRemove = state == TransferState::Finish ||
                            state == TransferState::Cancelled ||
                            state == TransferState::Disconnected ||
                            state == TransferState::Idle;

        mSenderRemoveAction->setEnabled(enableRemove);
        mSenderPauseAction->setEnabled(ti->canPause());
        mSenderResumeAction->setEnabled(ti->canResume());
        mSenderCancelAction->setEnabled(ti->canCancel());

        contextMenu.addAction(mSenderOpenAction);
        contextMenu.addAction(mSenderOpenFolderAction);
        contextMenu.addSeparator();
        contextMenu.addAction(mSendFilesAction);
        contextMenu.addAction(mSendFolderAction);
        contextMenu.addSeparator();
        contextMenu.addAction(mSenderRemoveAction);
        contextMenu.addAction(mSenderClearAction);
        contextMenu.addSeparator();
        contextMenu.addAction(mSenderPauseAction);
        contextMenu.addAction(mSenderResumeAction);
        contextMenu.addAction(mSenderCancelAction);
    }
    else {
        contextMenu.addAction(mSendFilesAction);
        contextMenu.addAction(mSendFolderAction);
        contextMenu.addSeparator();
        contextMenu.addAction(mSenderClearAction);
    }

    QPoint globPos = ui->senderTableView->mapToGlobal(pos);
    contextMenu.exec(globPos);
}

void MainWindow::onReceiverTableContextMenuRequested(const QPoint& pos)
{
    QModelIndex currIndex = ui->receiverTableView->indexAt(pos);
    QMenu contextMenu;

    if (currIndex.isValid()) {
        TransferInfo* ti = mReceiverModel->getTransferInfo(currIndex.row());
        TransferState state = ti->getState();
        bool enableFileMenu = state == TransferState::Finish;
        bool enableRemove = state == TransferState::Finish ||
                            state == TransferState::Cancelled ||
                            state == TransferState::Disconnected ||
                            state == TransferState::Idle;

        mRecOpenAction->setEnabled(enableFileMenu);
        mRecOpenFolderAction->setEnabled(enableFileMenu);
        mRecRemoveAction->setEnabled(enableFileMenu | enableRemove);
        mRecDeleteAction->setEnabled(enableFileMenu);
        mRecPauseAction->setEnabled(ti->canPause());
        mRecResumeAction->setEnabled(ti->canResume());
        mRecCancelAction->setEnabled(ti->canCancel());

        contextMenu.addAction(mRecOpenAction);
        contextMenu.addAction(mRecOpenFolderAction);
        contextMenu.addAction(mRecRemoveAction);
        contextMenu.addAction(mRecDeleteAction);
        contextMenu.addAction(mRecClearAction);
        contextMenu.addSeparator();
        contextMenu.addAction(mRecPauseAction);
        contextMenu.addAction(mRecResumeAction);
        contextMenu.addAction(mRecCancelAction);
    }
    else {
        contextMenu.addAction(mRecClearAction);
    }

    QPoint globPos = ui->receiverTableView->mapToGlobal(pos);
    contextMenu.exec(globPos);
}

void MainWindow::openSenderFileInCurrentIndex()
{
    QModelIndex currIndex = ui->senderTableView->currentIndex();
    QModelIndex fileNameIndex = mSenderModel->index(currIndex.row(), (int)TransferTableModel::Column::FileName);
    QString fileName = mSenderModel->data(fileNameIndex).toString();

    QDesktopServices::openUrl(QUrl::fromLocalFile(fileName));
}

void MainWindow::openSenderFolderInCurrentIndex()
{
    QModelIndex currIndex = ui->senderTableView->currentIndex();
    QModelIndex fileNameIndex = mSenderModel->index(currIndex.row(), (int)TransferTableModel::Column::FileName);
    QString dir = QFileInfo(mSenderModel->data(fileNameIndex).toString()).absoluteDir().absolutePath();

    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::removeSenderItemInCurrentIndex()
{
    QModelIndex currIndex = ui->senderTableView->currentIndex();
    mSenderModel->removeTransfer(currIndex.row());
    updateEmptyStates();
    scheduleUpdateStatusBar();
}

void MainWindow::openReceiverFileInCurrentIndex()
{
    QModelIndex currIndex = ui->receiverTableView->currentIndex();
    QModelIndex fileNameIndex = mReceiverModel->index(currIndex.row(), (int)TransferTableModel::Column::FileName);
    QString fileName = mReceiverModel->data(fileNameIndex).toString();

    QDesktopServices::openUrl(QUrl::fromLocalFile(fileName));
}

void MainWindow::openReceiverFolderInCurrentIndex()
{
    QModelIndex currIndex = ui->receiverTableView->currentIndex();
    QModelIndex fileNameIndex = mReceiverModel->index(currIndex.row(), (int)TransferTableModel::Column::FileName);
    QString dir = QFileInfo(mReceiverModel->data(fileNameIndex).toString()).absoluteDir().absolutePath();

    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::removeReceiverItemInCurrentIndex()
{
    QModelIndex currIndex = ui->receiverTableView->currentIndex();
    mReceiverModel->removeTransfer(currIndex.row());
    updateEmptyStates();
    scheduleUpdateStatusBar();
}

void MainWindow::deleteReceiverFileInCurrentIndex()
{
    QModelIndex currIndex = ui->receiverTableView->currentIndex();
    QModelIndex fileNameIndex = mReceiverModel->index(currIndex.row(), (int)TransferTableModel::Column::FileName);
    QString fileName = mReceiverModel->data(fileNameIndex).toString();

    QString str = "Are you sure wants to delete<p>" + fileName + "?";
    QMessageBox::StandardButton ret = QMessageBox::question(this, tr("Delete"), str);
    if (ret == QMessageBox::Yes) {
        QFile::remove(fileName);
        mReceiverModel->removeTransfer(currIndex.row());
        updateEmptyStates();
        scheduleUpdateStatusBar();
    }
}

void MainWindow::onSelectedSenderStateChanged(TransferState state)
{
    ui->resumeSenderBtn->setEnabled(state == TransferState::Paused);
    ui->pauseSenderBtn->setEnabled(state == TransferState::Transfering || state == TransferState::Waiting);
    ui->cancelSenderBtn->setEnabled(state == TransferState::Transfering || state == TransferState::Waiting ||
                                    state == TransferState::Paused || state == TransferState::Queued);
}

void MainWindow::onSelectedReceiverStateChanged(TransferState state)
{
    ui->resumeReceiverBtn->setEnabled(state == TransferState::Paused);
    ui->pauseReceiverBtn->setEnabled(state == TransferState::Transfering || state == TransferState::Waiting);
    ui->cancelReceiverBtn->setEnabled(state == TransferState::Transfering || state == TransferState::Waiting ||
                                    state == TransferState::Paused);
}

void MainWindow::quitApp()
{
    mForceQuit = true;
    close();
}

void MainWindow::setupToolbar()
{
    QMenu* sendMenu = new QMenu();
    sendMenu->addAction(mSendFilesAction);
    sendMenu->addAction(mSendFolderAction);

    QToolButton* sendBtn = new QToolButton();
    sendBtn->setPopupMode(QToolButton::InstantPopup);
    sendBtn->setToolButtonStyle(Qt::ToolButtonTextUnderIcon);
    sendBtn->setText(tr("Send"));
    sendBtn->setIcon(UiTheme::appIcon(QStringLiteral(":/img/send.png")));
    sendBtn->setMenu(sendMenu);
    ui->mainToolBar->addWidget(sendBtn);
    ui->mainToolBar->addSeparator();

    ui->mainToolBar->addAction(mSettingsAction);

    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    ui->mainToolBar->addWidget(spacer);

    QMenu* menu = new QMenu();
    menu->addAction(mViewLogAction);
    menu->addSeparator();
    menu->addAction(mAboutAction);
    menu->addAction(mAboutQtAction);

    QToolButton* aboutBtn = new QToolButton();
    aboutBtn->setText(tr("About"));
    aboutBtn->setToolTip(tr("About this program"));
    aboutBtn->setIcon(UiTheme::appIcon(QStringLiteral(":/img/about.png")));
    aboutBtn->setMenu(menu);
    aboutBtn->setPopupMode(QToolButton::InstantPopup);
    ui->mainToolBar->addWidget(aboutBtn);
}

void MainWindow::setupActions()
{
    mShowMainWindowAction = new QAction(tr("Show Main Window"), this);
    connect(mShowMainWindowAction, &QAction::triggered, this, &MainWindow::onShowMainWindowTriggered);
    mSendFilesAction = new QAction(
        UiTheme::appIcon(QStringLiteral(":/img/file.png")),
        tr("Send files..."), this);
    connect(mSendFilesAction, &QAction::triggered, this, &MainWindow::onSendFilesActionTriggered);
    mSendFolderAction = new QAction(
        UiTheme::appIcon(QStringLiteral(":/img/folder.png")),
        tr("Send folders..."), this);
    connect(mSendFolderAction, &QAction::triggered, this, &MainWindow::onSendFolderActionTriggered);
    mSettingsAction = new QAction(
        UiTheme::appIcon(QStringLiteral(":/img/settings.png")),
        tr("Settings"), this);
    connect(mSettingsAction, &QAction::triggered, this, &MainWindow::onSettingsActionTriggered);
    mViewLogAction = new QAction(tr("View Log..."), this);
    connect(mViewLogAction, &QAction::triggered, this, &MainWindow::onViewLogActionTriggered);
    mAboutAction = new QAction(
        UiTheme::appIcon(QStringLiteral(":/img/about.png")),
        tr("About"), this);
    mAboutAction->setMenuRole(QAction::AboutRole);
    connect(mAboutAction, &QAction::triggered, this, &MainWindow::onAboutActionTriggered);
    mAboutQtAction = new QAction(tr("About Qt"), this);
    mAboutQtAction->setMenuRole(QAction::AboutQtRole);
    connect(mAboutQtAction, &QAction::triggered, QApplication::instance(), &QApplication::aboutQt);
    mQuitAction = new QAction(tr("Quit"), this);
    connect(mQuitAction, &QAction::triggered, this, &MainWindow::quitApp);

    mSenderOpenAction = new QAction(tr("Open"), this);
    connect(mSenderOpenAction, &QAction::triggered, this, &MainWindow::openSenderFileInCurrentIndex);
    mSenderOpenFolderAction = new QAction(tr("Open folder"), this);
    connect(mSenderOpenFolderAction, &QAction::triggered, this, &MainWindow::openSenderFolderInCurrentIndex);
    mSenderRemoveAction = new QAction(
        UiTheme::appIcon(QStringLiteral(":/img/remove.png")),
        tr("Remove"), this);
    connect(mSenderRemoveAction, &QAction::triggered, this, &MainWindow::removeSenderItemInCurrentIndex);
    mSenderClearAction = new QAction(
        UiTheme::appIcon(QStringLiteral(":/img/clear.png")),
        tr("Clear"), this);
    connect(mSenderClearAction, &QAction::triggered, this, &MainWindow::onSenderClearClicked);
    mSenderPauseAction = new QAction(
        UiTheme::appIcon(QStringLiteral(":/img/pause.png")),
        tr("Pause"), this);
    connect(mSenderPauseAction, &QAction::triggered, this, &MainWindow::onSenderPauseClicked);
    mSenderResumeAction = new QAction(
        UiTheme::appIcon(QStringLiteral(":/img/resume.png")),
        tr("Resume"), this);
    connect(mSenderResumeAction, &QAction::triggered, this, &MainWindow::onSenderResumeClicked);
    mSenderCancelAction = new QAction(
        UiTheme::appIcon(QStringLiteral(":/img/cancel.png")),
        tr("Cancel"), this);
    connect(mSenderCancelAction, &QAction::triggered, this, &MainWindow::onSenderCancelClicked);

    mRecOpenAction = new QAction(tr("Open"), this);
    connect(mRecOpenAction, &QAction::triggered, this, &MainWindow::openReceiverFileInCurrentIndex);
    mRecOpenFolderAction = new QAction(tr("Open folder"), this);
    connect(mRecOpenFolderAction, &QAction::triggered, this, &MainWindow::openReceiverFolderInCurrentIndex);
    mRecRemoveAction = new QAction(
        UiTheme::appIcon(QStringLiteral(":/img/remove.png")),
        tr("Remove"), this);
    connect(mRecRemoveAction, &QAction::triggered, this, &MainWindow::removeReceiverItemInCurrentIndex);
    mRecDeleteAction = new QAction(tr("Delete from disk"), this);
    connect(mRecDeleteAction, &QAction::triggered, this, &MainWindow::deleteReceiverFileInCurrentIndex);
    mRecClearAction = new QAction(
        UiTheme::appIcon(QStringLiteral(":/img/clear.png")),
        tr("Clear"), this);
    connect(mRecClearAction, &QAction::triggered, this, &MainWindow::onReceiverClearClicked);
    mRecPauseAction = new QAction(
        UiTheme::appIcon(QStringLiteral(":/img/pause.png")),
        tr("Pause"), this);
    connect(mRecPauseAction, &QAction::triggered, this, &MainWindow::onReceiverPauseClicked);
    mRecResumeAction = new QAction(
        UiTheme::appIcon(QStringLiteral(":/img/resume.png")),
        tr("Resume"), this);
    connect(mRecResumeAction, &QAction::triggered, this, &MainWindow::onReceiverResumeClicked);
    mRecCancelAction = new QAction(
        UiTheme::appIcon(QStringLiteral(":/img/cancel.png")),
        tr("Cancel"), this);
    connect(mRecCancelAction, &QAction::triggered, this, &MainWindow::onReceiverCancelClicked);
}

void MainWindow::setupSystrayIcon()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        mSystrayIcon = nullptr;
        return;
    }

    mSystrayMenu = new QMenu(this);
    mSystrayMenu->addAction(mShowMainWindowAction);
    mSystrayMenu->addSeparator();
    mSystrayMenu->addAction(mSendFilesAction);
    mSystrayMenu->addAction(mSendFolderAction);
    mSystrayMenu->addSeparator();
    mSystrayMenu->addAction(mViewLogAction);
    mSystrayMenu->addAction(mAboutAction);
    mSystrayMenu->addAction(mAboutQtAction);
    mSystrayMenu->addSeparator();
    mSystrayMenu->addAction(mQuitAction);

    mSystrayIcon = new QSystemTrayIcon(QIcon(":/img/systray-icon.png"), this);
    mSystrayIcon->setToolTip(PROGRAM_NAME);
    mSystrayIcon->setContextMenu(mSystrayMenu);
    mSystrayIcon->show();
}

void MainWindow::showStartupMessage(const QString& summary)
{
    if (!summary.isEmpty())
        statusBar()->showMessage(summary, 5000);
}

void MainWindow::setupTableStacks()
{
    const auto wrapTable = [](QTableView* tableView) -> QStackedWidget* {
        QWidget* parent = tableView->parentWidget();
        auto* layout = qobject_cast<QVBoxLayout*>(parent->layout());
        const int index = layout->indexOf(tableView);
        layout->removeWidget(tableView);

        auto* stack = new QStackedWidget(parent);
        layout->insertWidget(index, stack);
        stack->addWidget(tableView);
        return stack;
    };

    mSenderStack = wrapTable(ui->senderTableView);
    auto* senderEmpty = new QLabel(tr("No uploads yet. Drop files here or use Send."), mSenderStack);
    senderEmpty->setObjectName(QStringLiteral("senderEmptyLabel"));
    senderEmpty->setAlignment(Qt::AlignCenter);
    senderEmpty->setWordWrap(true);
    {
        QPalette pal = senderEmpty->palette();
        pal.setColor(QPalette::WindowText, palette().color(QPalette::PlaceholderText));
        senderEmpty->setPalette(pal);
    }
    mSenderStack->addWidget(senderEmpty);

    mReceiverStack = wrapTable(ui->receiverTableView);
    auto* receiverEmpty = new QLabel(tr("No downloads yet. Incoming transfers appear here."), mReceiverStack);
    receiverEmpty->setObjectName(QStringLiteral("receiverEmptyLabel"));
    receiverEmpty->setAlignment(Qt::AlignCenter);
    receiverEmpty->setWordWrap(true);
    {
        QPalette pal = receiverEmpty->palette();
        pal.setColor(QPalette::WindowText, palette().color(QPalette::PlaceholderText));
        receiverEmpty->setPalette(pal);
    }
    mReceiverStack->addWidget(receiverEmpty);
}

void MainWindow::setupTablePolish()
{
    const auto polishTable = [](QTableView* tableView) {
        tableView->setAlternatingRowColors(true);
        tableView->setShowGrid(false);
        tableView->verticalHeader()->setDefaultSectionSize(52);

        QHeaderView* header = tableView->horizontalHeader();
        header->setStretchLastSection(false);
        header->setSectionResizeMode((int)TransferTableModel::Column::FileName, QHeaderView::Stretch);
        header->setSectionResizeMode((int)TransferTableModel::Column::Progress, QHeaderView::Fixed);
        tableView->setColumnWidth((int)TransferTableModel::Column::Progress, 200);
    };

    polishTable(ui->senderTableView);
    polishTable(ui->receiverTableView);
}

void MainWindow::setupAccessibility()
{
    ui->resumeSenderBtn->setAccessibleName(tr("Resume upload"));
    ui->pauseSenderBtn->setAccessibleName(tr("Pause upload"));
    ui->cancelSenderBtn->setAccessibleName(tr("Cancel upload"));
    ui->pushButton_2->setAccessibleName(tr("Clear completed uploads"));

    ui->resumeReceiverBtn->setAccessibleName(tr("Resume download"));
    ui->pauseReceiverBtn->setAccessibleName(tr("Pause download"));
    ui->cancelReceiverBtn->setAccessibleName(tr("Cancel download"));
    ui->pushButton->setAccessibleName(tr("Clear completed downloads"));
}

void MainWindow::connectTransferModelSignals()
{
    const auto connectModel = [this](TransferTableModel* model) {
        connect(model, &QAbstractItemModel::rowsInserted,
                this, &MainWindow::onTransferRowsInserted);
        connect(model, &QAbstractItemModel::rowsRemoved,
                this, &MainWindow::onTransferRowsRemoved);
    };

    connectModel(mSenderModel);
    connectModel(mReceiverModel);
}

void MainWindow::onTransferRowsInserted(const QModelIndex& parent, int first, int last)
{
    Q_UNUSED(parent);

    auto* model = qobject_cast<TransferTableModel*>(sender());
    if (!model)
        return;

    for (int i = first; i <= last; ++i) {
        TransferInfo* info = model->getTransferInfo(i);
        connect(info, &TransferInfo::stateChanged, this, [this](TransferState) {
            scheduleUpdateStatusBar();
        });
    }

    updateEmptyStates();
    scheduleUpdateStatusBar();
}

void MainWindow::onTransferRowsRemoved(const QModelIndex& parent, int first, int last)
{
    Q_UNUSED(parent);
    Q_UNUSED(first);
    Q_UNUSED(last);

    updateEmptyStates();
    scheduleUpdateStatusBar();
}

int MainWindow::countActiveTransfers(const TransferTableModel* model)
{
    int count = 0;
    const int rows = model->rowCount();
    for (int i = 0; i < rows; ++i) {
        const TransferState state = model->getTransferInfo(i)->getState();
        if (state == TransferState::Waiting ||
                state == TransferState::Transfering ||
                state == TransferState::Paused ||
                state == TransferState::Queued) {
            ++count;
        }
    }
    return count;
}

void MainWindow::updateStatusBar()
{
    const int port = Settings::instance()->getTransferPort();
    const QString tls = Settings::instance()->getTlsEnabled() ? tr("on") : tr("off");
    const int uploads = countActiveTransfers(mSenderModel);
    const int downloads = countActiveTransfers(mReceiverModel);
    mStatusLabel->setText(tr("Listening on port %1 | TLS %2 | Uploads: %3 | Downloads: %4")
                              .arg(port)
                              .arg(tls)
                              .arg(uploads)
                              .arg(downloads));
}

void MainWindow::scheduleUpdateStatusBar()
{
    QTimer::singleShot(0, this, &MainWindow::updateStatusBar);
}

void MainWindow::updateEmptyStates()
{
    if (mSenderStack)
        mSenderStack->setCurrentIndex(mSenderModel->rowCount() == 0 ? 1 : 0);
    if (mReceiverStack)
        mReceiverStack->setCurrentIndex(mReceiverModel->rowCount() == 0 ? 1 : 0);
}
