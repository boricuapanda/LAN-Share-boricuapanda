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
#include <QStatusBar>
#include <QTimer>
#include <QVBoxLayout>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QFileInfo>
#include <QDir>
#include <QPushButton>
#include <QHBoxLayout>
#include <QStackedWidget>
#include <QHostInfo>
#include <QSizePolicy>

#include "mainwindow.h"
#include "ui_mainwindow.h"

#include "receiverselectordialog.h"
#include "settingsdialog.h"
#include "settings.h"
#include "aboutpage.h"
#include "logviewerdialog.h"
#include "util.h"
#include "transferlistpanel.h"
#include "transfercardwidget.h"
#include "ui/uitheme.h"
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
    setupSidebarBranding();
    setupSystrayIcon();
    setWindowTitle(PROGRAM_NAME);

    mBroadcaster = new DeviceBroadcaster(this);
    mBroadcaster->start();
    mSenderModel = new TransferTableModel(this);
    mReceiverModel = new TransferTableModel(this);
    mDeviceModel = new DeviceListModel(mBroadcaster, this);
    mTransServer = new TransferServer(mDeviceModel, this);
    mTransServer->listen();

    ui->senderTableView->hide();
    ui->receiverTableView->hide();
    ui->label->setObjectName(QStringLiteral("sectionHeader"));
    ui->label_2->setObjectName(QStringLiteral("sectionHeader"));

    setupContentHeader();
    setupTransferPanels();
    setupContentStack();
    setupAccessibility();

    setAcceptDrops(true);
    if (mSenderPanel && mSenderPanel->dropTargetWidget()) {
        mSenderPanel->dropTargetWidget()->setAcceptDrops(true);
        mSenderPanel->dropTargetWidget()->installEventFilter(this);
    }

    mStatusLabel = new QLabel(this);
    statusBar()->addWidget(mStatusLabel, 1);

    connectTransferModelSignals();
    connectSignals();
    updateActiveBadge();
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

    connect(mSenderPanel, &TransferListPanel::currentRowChanged,
            this, &MainWindow::onSenderPanelSelectionChanged);
    connect(mReceiverPanel, &TransferListPanel::currentRowChanged,
            this, &MainWindow::onReceiverPanelSelectionChanged);
    connect(mSenderPanel, &TransferListPanel::activated,
            this, &MainWindow::onSenderPanelActivated);
    connect(mReceiverPanel, &TransferListPanel::activated,
            this, &MainWindow::onReceiverPanelActivated);
    connect(mSenderPanel, &TransferListPanel::contextMenuRequested,
            this, &MainWindow::onSenderPanelContextMenu);
    connect(mReceiverPanel, &TransferListPanel::contextMenuRequested,
            this, &MainWindow::onReceiverPanelContextMenu);
    connect(mSenderPanel, &TransferListPanel::countChanged, this, [this](int) {
        updateActiveBadge();
        scheduleUpdateStatusBar();
    });
    connect(mReceiverPanel, &TransferListPanel::countChanged, this, [this](int) {
        updateActiveBadge();
        scheduleUpdateStatusBar();
    });
}

void MainWindow::sendFile(const QString& folderName, const QString &filePath, const Device &receiver)
{
    Sender* sender = new Sender(receiver, folderName, filePath, this);
    mSenderModel->insertTransfer(sender);

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
    if (watched == mSenderPanel || watched == mSenderPanel->dropTargetWidget()) {
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
    showSettingsView();
}

void MainWindow::onTransfersActionTriggered()
{
    showTransfersView();
}

void MainWindow::onAboutActionTriggered()
{
    setMainWindowVisibility(true);
    showAboutView();
}

void MainWindow::onViewLogActionTriggered()
{
    LogViewerDialog dialog(this);
    dialog.exec();
}

void MainWindow::onNewReceiverAdded(Receiver *rec)
{
    mReceiverModel->insertTransfer(rec);
    scheduleUpdateStatusBar();
}

void MainWindow::onSenderPanelSelectionChanged(int row)
{
    if (row >= 0 && row < mSenderModel->rowCount()) {
        TransferInfo* ti = mSenderModel->getTransferInfo(row);
        ui->resumeSenderBtn->setEnabled(ti->canResume());
        ui->pauseSenderBtn->setEnabled(ti->canPause());
        ui->cancelSenderBtn->setEnabled(ti->canCancel());
        connect(ti, &TransferInfo::stateChanged, this, &MainWindow::onSelectedSenderStateChanged,
                static_cast<Qt::ConnectionType>(Qt::UniqueConnection));
    } else {
        ui->resumeSenderBtn->setEnabled(false);
        ui->pauseSenderBtn->setEnabled(false);
        ui->cancelSenderBtn->setEnabled(false);
    }
}

void MainWindow::onReceiverPanelSelectionChanged(int row)
{
    if (row >= 0 && row < mReceiverModel->rowCount()) {
        TransferInfo* ti = mReceiverModel->getTransferInfo(row);
        ui->resumeReceiverBtn->setEnabled(ti->canResume());
        ui->pauseReceiverBtn->setEnabled(ti->canPause());
        ui->cancelReceiverBtn->setEnabled(ti->canCancel());
        connect(ti, &TransferInfo::stateChanged, this, &MainWindow::onSelectedReceiverStateChanged,
                static_cast<Qt::ConnectionType>(Qt::UniqueConnection));
    } else {
        ui->resumeReceiverBtn->setEnabled(false);
        ui->pauseReceiverBtn->setEnabled(false);
        ui->cancelReceiverBtn->setEnabled(false);
    }
}

void MainWindow::onSenderPanelActivated(int row)
{
    Q_UNUSED(row);
    openSenderFileInCurrentIndex();
}

void MainWindow::onReceiverPanelActivated(int row)
{
    if (row < 0 || row >= mReceiverModel->rowCount())
        return;
    TransferInfo* ti = mReceiverModel->getTransferInfo(row);
    if (ti && ti->getState() == TransferState::Finish)
        openReceiverFileInCurrentIndex();
}

void MainWindow::onSenderPanelContextMenu(const QPoint& globalPos, int row)
{
    Q_UNUSED(row);
    QMenu contextMenu;

    if (currentSenderRow() >= 0) {
        TransferInfo* ti = mSenderModel->getTransferInfo(currentSenderRow());
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
    } else {
        contextMenu.addAction(mSendFilesAction);
        contextMenu.addAction(mSendFolderAction);
        contextMenu.addSeparator();
        contextMenu.addAction(mSenderClearAction);
    }

    contextMenu.exec(globalPos);
}

void MainWindow::onReceiverPanelContextMenu(const QPoint& globalPos, int row)
{
    Q_UNUSED(row);
    QMenu contextMenu;

    if (currentReceiverRow() >= 0) {
        TransferInfo* ti = mReceiverModel->getTransferInfo(currentReceiverRow());
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
    } else {
        contextMenu.addAction(mRecClearAction);
    }

    contextMenu.exec(globalPos);
}

void MainWindow::onSenderClearClicked()
{
    mSenderModel->clearCompleted();
    scheduleUpdateStatusBar();
}

void MainWindow::onSenderCancelClicked()
{
    const int row = currentSenderRow();
    if (row >= 0)
        mSenderModel->getTransfer(row)->cancel();
}

void MainWindow::onSenderPauseClicked()
{
    const int row = currentSenderRow();
    if (row >= 0)
        mSenderModel->getTransfer(row)->pause();
}

void MainWindow::onSenderResumeClicked()
{
    const int row = currentSenderRow();
    if (row >= 0)
        mSenderModel->getTransfer(row)->resume();
}

void MainWindow::onReceiverClearClicked()
{
    mReceiverModel->clearCompleted();
    scheduleUpdateStatusBar();
}

void MainWindow::onReceiverCancelClicked()
{
    const int row = currentReceiverRow();
    if (row >= 0)
        mReceiverModel->getTransfer(row)->cancel();
}

void MainWindow::onReceiverPauseClicked()
{
    const int row = currentReceiverRow();
    if (row >= 0)
        mReceiverModel->getTransfer(row)->pause();
}

void MainWindow::onReceiverResumeClicked()
{
    const int row = currentReceiverRow();
    if (row >= 0)
        mReceiverModel->getTransfer(row)->resume();
}

void MainWindow::openSenderFileInCurrentIndex()
{
    const int row = currentSenderRow();
    if (row < 0)
        return;
    const QString fileName = mSenderModel->getTransferInfo(row)->getFilePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(fileName));
}

void MainWindow::openSenderFolderInCurrentIndex()
{
    const int row = currentSenderRow();
    if (row < 0)
        return;
    const QString dir = QFileInfo(mSenderModel->getTransferInfo(row)->getFilePath()).absoluteDir().absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::removeSenderItemInCurrentIndex()
{
    const int row = currentSenderRow();
    if (row >= 0)
        mSenderModel->removeTransfer(row);
    scheduleUpdateStatusBar();
}

void MainWindow::openReceiverFileInCurrentIndex()
{
    const int row = currentReceiverRow();
    if (row < 0)
        return;
    const QString fileName = mReceiverModel->getTransferInfo(row)->getFilePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(fileName));
}

void MainWindow::openReceiverFolderInCurrentIndex()
{
    const int row = currentReceiverRow();
    if (row < 0)
        return;
    const QString dir = QFileInfo(mReceiverModel->getTransferInfo(row)->getFilePath()).absoluteDir().absolutePath();
    QDesktopServices::openUrl(QUrl::fromLocalFile(dir));
}

void MainWindow::removeReceiverItemInCurrentIndex()
{
    const int row = currentReceiverRow();
    if (row >= 0)
        mReceiverModel->removeTransfer(row);
    scheduleUpdateStatusBar();
}

void MainWindow::deleteReceiverFileInCurrentIndex()
{
    const int row = currentReceiverRow();
    if (row < 0)
        return;
    const QString fileName = mReceiverModel->getTransferInfo(row)->getFilePath();

    QString str = "Are you sure wants to delete<p>" + fileName + "?";
    QMessageBox::StandardButton ret = QMessageBox::question(this, tr("Delete"), str);
    if (ret == QMessageBox::Yes) {
        QFile::remove(fileName);
        mReceiverModel->removeTransfer(row);
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
    ui->mainToolBar->setObjectName(QStringLiteral("mainSideBar"));
    ui->mainToolBar->setMinimumWidth(220);
    ui->mainToolBar->setIconSize(QSize(22, 22));
    ui->mainToolBar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    mNavTransfersBtn = createSidebarNavButton(
        tr("Transfers"),
        UiTheme::appIcon(QStringLiteral(":/img/up.png")),
        true);
    connect(mNavTransfersBtn, &QToolButton::clicked, this, &MainWindow::onTransfersActionTriggered);
    ui->mainToolBar->addWidget(mNavTransfersBtn);

    QToolButton* logBtn = createSidebarNavButton(
        tr("Log Viewer"),
        UiTheme::appIcon(QStringLiteral(":/img/file.png")),
        false);
    connect(logBtn, &QToolButton::clicked, this, &MainWindow::onViewLogActionTriggered);
    ui->mainToolBar->addWidget(logBtn);

    mNavSettingsBtn = createSidebarNavButton(
        tr("Settings"),
        UiTheme::appIcon(QStringLiteral(":/img/settings.png")),
        true);
    connect(mNavSettingsBtn, &QToolButton::clicked, this, &MainWindow::onSettingsActionTriggered);
    ui->mainToolBar->addWidget(mNavSettingsBtn);

    mNavAboutBtn = createSidebarNavButton(
        tr("About"),
        UiTheme::appIcon(QStringLiteral(":/img/about.png")),
        true);
    connect(mNavAboutBtn, &QToolButton::clicked, this, &MainWindow::onAboutActionTriggered);
    ui->mainToolBar->addWidget(mNavAboutBtn);

    QWidget* spacer = new QWidget();
    spacer->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Expanding);
    ui->mainToolBar->addWidget(spacer);

    updateSidebarNavSelection(ContentView::Transfers);
}

QToolButton* MainWindow::createSidebarNavButton(const QString& text, const QIcon& icon, bool checkable)
{
    auto* btn = new QToolButton(this);
    btn->setObjectName(QStringLiteral("sidebarNavBtn"));
    btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    btn->setText(text);
    btn->setIcon(icon);
    btn->setCheckable(checkable);
    btn->setAutoExclusive(false);
    btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return btn;
}

void MainWindow::updateSidebarNavSelection(ContentView activeView)
{
    if (mNavTransfersBtn)
        mNavTransfersBtn->setChecked(activeView == ContentView::Transfers);
    if (mNavSettingsBtn)
        mNavSettingsBtn->setChecked(activeView == ContentView::Settings);
    if (mNavAboutBtn)
        mNavAboutBtn->setChecked(activeView == ContentView::About);
    if (mSettingsAction)
        mSettingsAction->setVisible(activeView != ContentView::Settings);
    if (mTransfersAction)
        mTransfersAction->setVisible(activeView != ContentView::Transfers);
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
    mTransfersAction = new QAction(
        UiTheme::appIcon(QStringLiteral(":/img/up.png")),
        tr("Transfers"), this);
    connect(mTransfersAction, &QAction::triggered, this, &MainWindow::onTransfersActionTriggered);
    mViewLogAction = new QAction(
        UiTheme::appIcon(QStringLiteral(":/img/file.png")),
        tr("Log Viewer"), this);
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

void MainWindow::setupSidebarBranding()
{
    auto* brand = new QWidget();
    auto* brandLayout = new QHBoxLayout(brand);
    brandLayout->setContentsMargins(4, 0, 12, 0);
    brandLayout->setSpacing(8);

    auto* icon = new QLabel(brand);
    icon->setPixmap(UiTheme::appIcon(QStringLiteral(":/img/icon.png")).pixmap(24, 24));
    brandLayout->addWidget(icon);

    auto* name = new QLabel(PROGRAM_NAME, brand);
    name->setObjectName(QStringLiteral("sidebarBrandName"));
    QFont nameFont = name->font();
    nameFont.setBold(true);
    name->setFont(nameFont);
    brandLayout->addWidget(name);

    auto* version = new QLabel(QStringLiteral("v%1").arg(Util::parseAppVersion(true)), brand);
    version->setObjectName(QStringLiteral("sidebarVersion"));
    brandLayout->addWidget(version);

    const QList<QAction*> toolbarActions = ui->mainToolBar->actions();
    QAction* firstAction = toolbarActions.isEmpty() ? nullptr : toolbarActions.first();
    if (firstAction) {
        ui->mainToolBar->insertWidget(firstAction, brand);
        ui->mainToolBar->insertSeparator(firstAction);
    } else {
        ui->mainToolBar->addWidget(brand);
        ui->mainToolBar->addSeparator();
    }

    Settings* settings = Settings::instance();
    const QString hostName = settings->getDeviceName().isEmpty()
            ? QHostInfo::localHostName()
            : settings->getDeviceName();
    const QString profile = tr("%1 - %2").arg(hostName, Device::formatAddress(settings->getDeviceAddress()));
    auto* profileLabel = new QLabel(profile);
    profileLabel->setObjectName(QStringLiteral("sidebarProfile"));

    for (QAction* action : ui->mainToolBar->actions()) {
        QWidget* widget = ui->mainToolBar->widgetForAction(action);
        if (widget && widget->sizePolicy().verticalPolicy() == QSizePolicy::Expanding) {
            ui->mainToolBar->insertWidget(action, profileLabel);
            break;
        }
    }
}

void MainWindow::setupContentHeader()
{
    auto* header = new QWidget(ui->centralWidget);
    header->setObjectName(QStringLiteral("contentHeader"));
    mContentHeader = header;

    auto* layout = new QHBoxLayout(header);
    layout->setContentsMargins(18, 10, 18, 10);
    layout->setSpacing(12);

    auto* title = new QLabel(tr("Transfers"), header);
    title->setObjectName(QStringLiteral("contentTitle"));
    mContentTitle = title;
    QFont titleFont = title->font();
    titleFont.setPointSize(titleFont.pointSize() + 2);
    titleFont.setBold(true);
    title->setFont(titleFont);
    layout->addWidget(title);

    mActiveBadge = new QLabel(header);
    mActiveBadge->setObjectName(QStringLiteral("activeBadge"));
    layout->addWidget(mActiveBadge);

    layout->addStretch();

    auto* sendMenu = new QMenu(header);
    sendMenu->setObjectName(QStringLiteral("sendMenu"));
    sendMenu->addAction(mSendFilesAction);
    sendMenu->addAction(mSendFolderAction);

    auto* sendBtn = new QToolButton(header);
    sendBtn->setProperty("primary", true);
    sendBtn->setObjectName(QStringLiteral("headerSendBtn"));
    sendBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    sendBtn->setText(tr("Send"));
    sendBtn->setIcon(UiTheme::appIcon(QStringLiteral(":/img/send.png")));
    sendBtn->setIconSize(QSize(16, 16));
    sendBtn->setPopupMode(QToolButton::MenuButtonPopup);
    sendBtn->setMenu(sendMenu);
    connect(sendBtn, &QToolButton::clicked, mSendFilesAction, &QAction::trigger);
    mHeaderSendButton = sendBtn;
    layout->addWidget(sendBtn);

    ui->verticalLayout_3->insertWidget(0, header);
}

void MainWindow::setupTransferPanels()
{
    if (ui->splitter->count() >= 2) {
        ui->splitter->widget(0)->setObjectName(QStringLiteral("transferSection"));
        ui->splitter->widget(1)->setObjectName(QStringLiteral("transferSection"));
    }

    ui->splitter->setHandleWidth(10);
    ui->splitter->setChildrenCollapsible(false);

    if (ui->centralWidget->layout())
        ui->centralWidget->layout()->setContentsMargins(6, 6, 6, 6);
    if (ui->verticalLayout)
        ui->verticalLayout->setSpacing(4);
    if (ui->verticalLayout_2)
        ui->verticalLayout_2->setSpacing(4);
    if (ui->verticalLayout)
        ui->verticalLayout->setContentsMargins(8, 8, 8, 8);
    if (ui->verticalLayout_2)
        ui->verticalLayout_2->setContentsMargins(8, 8, 8, 8);
    if (ui->horizontalLayout) {
        ui->horizontalLayout->setContentsMargins(10, 6, 10, 4);
        ui->horizontalLayout->setSpacing(4);
        ui->horizontalLayout->setAlignment(Qt::AlignVCenter);
    }
    if (ui->horizontalLayout_2) {
        ui->horizontalLayout_2->setContentsMargins(10, 6, 10, 4);
        ui->horizontalLayout_2->setSpacing(4);
        ui->horizontalLayout_2->setAlignment(Qt::AlignVCenter);
    }

    auto configureTransferActionButton = [](QPushButton* button) {
        if (!button)
            return;
        button->setFixedSize(28, 28);
        button->setIconSize(QSize(16, 16));
        button->setFocusPolicy(Qt::NoFocus);
        button->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        button->setContentsMargins(0, 0, 0, 0);
    };
    configureTransferActionButton(ui->resumeSenderBtn);
    configureTransferActionButton(ui->pauseSenderBtn);
    configureTransferActionButton(ui->cancelSenderBtn);
    configureTransferActionButton(ui->pushButton_2);
    configureTransferActionButton(ui->resumeReceiverBtn);
    configureTransferActionButton(ui->pauseReceiverBtn);
    configureTransferActionButton(ui->cancelReceiverBtn);
    configureTransferActionButton(ui->pushButton);

    auto configureTransferHeader = [](QHBoxLayout* layout, QLabel* iconLabel, QLabel* titleLabel,
                                      const QList<QPushButton*>& buttons) {
        if (!layout)
            return;

        if (iconLabel) {
            iconLabel->setFixedSize(18, 18);
            iconLabel->setScaledContents(true);
            layout->setAlignment(iconLabel, Qt::AlignVCenter);
        }
        if (titleLabel) {
            titleLabel->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
            layout->setAlignment(titleLabel, Qt::AlignVCenter);
        }
        for (QPushButton* button : buttons)
            if (button)
                layout->setAlignment(button, Qt::AlignVCenter);
    };
    configureTransferHeader(ui->horizontalLayout, ui->label_3, ui->label,
                            {ui->resumeSenderBtn, ui->pauseSenderBtn, ui->cancelSenderBtn, ui->pushButton_2});
    configureTransferHeader(ui->horizontalLayout_2, ui->label_4, ui->label_2,
                            {ui->resumeReceiverBtn, ui->pauseReceiverBtn, ui->cancelReceiverBtn, ui->pushButton});

    QWidget* uploadSection = ui->splitter->widget(0);
    QWidget* downloadSection = ui->splitter->widget(1);

    const int senderIdx = ui->verticalLayout->indexOf(ui->senderTableView);
    ui->verticalLayout->removeWidget(ui->senderTableView);

    mSenderPanel = new TransferListPanel(
        mSenderModel,
        tr("Uploads"),
        tr("No uploads yet.\nDrop files here or use Send."),
        QStringLiteral("senderEmptyLabel"),
        uploadSection);
    mSenderPanel->setObjectName(QStringLiteral("senderTransferPanel"));
    mSenderPanel->setHeaderVisible(false);
    ui->verticalLayout->insertWidget(senderIdx, mSenderPanel, 1);

    const int receiverIdx = ui->verticalLayout_2->indexOf(ui->receiverTableView);
    ui->verticalLayout_2->removeWidget(ui->receiverTableView);

    mReceiverPanel = new TransferListPanel(
        mReceiverModel,
        tr("Downloads"),
        tr("No downloads yet.\nIncoming transfers appear here."),
        QStringLiteral("receiverEmptyLabel"),
        downloadSection);
    mReceiverPanel->setObjectName(QStringLiteral("receiverTransferPanel"));
    mReceiverPanel->setHeaderVisible(false);
    ui->verticalLayout_2->insertWidget(receiverIdx, mReceiverPanel, 1);

    auto updateUploadTitle = [this](int count) {
        ui->label->setText(tr("Uploads (%1 total)").arg(count));
    };
    auto updateDownloadTitle = [this](int count) {
        ui->label_2->setText(tr("Downloads (%1 total)").arg(count));
    };
    connect(mSenderPanel, &TransferListPanel::countChanged, this, updateUploadTitle);
    connect(mReceiverPanel, &TransferListPanel::countChanged, this, updateDownloadTitle);
    updateUploadTitle(mSenderModel->rowCount());
    updateDownloadTitle(mReceiverModel->rowCount());
}

void MainWindow::setupContentStack()
{
    mTransfersPage = new QWidget(ui->centralWidget);
    mTransfersPage->setObjectName(QStringLiteral("transfersPage"));
    auto* transfersLayout = new QVBoxLayout(mTransfersPage);
    transfersLayout->setContentsMargins(0, 0, 0, 0);
    transfersLayout->setSpacing(6);

    if (mContentHeader) {
        ui->verticalLayout_3->removeWidget(mContentHeader);
        mContentHeader->setParent(ui->centralWidget);
    }

    while (ui->verticalLayout_3->count() > 0) {
        QLayoutItem* item = ui->verticalLayout_3->takeAt(0);
        if (!item)
            continue;

        if (QWidget* widget = item->widget()) {
            widget->setParent(mTransfersPage);
            transfersLayout->addWidget(widget, widget == ui->splitter ? 1 : 0);
            delete item;
        } else if (QLayout* layout = item->layout()) {
            transfersLayout->addLayout(layout);
            delete item;
        } else {
            transfersLayout->addItem(item);
        }
    }

    mContentStack = new QStackedWidget(ui->centralWidget);
    mContentStack->setObjectName(QStringLiteral("mainContentStack"));
    mContentStack->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    mContentStack->addWidget(mTransfersPage);

    mSettingsPanel = new SettingsDialog(mContentStack);
    mSettingsPanel->setObjectName(QStringLiteral("settingsPanel"));
    mContentStack->addWidget(mSettingsPanel);
    connect(mSettingsPanel, &SettingsDialog::cancelled, this, &MainWindow::showTransfersView);
    connect(mSettingsPanel, &SettingsDialog::saved, this, [this]() {
        updateStatusBar();
        showTransfersView();
    });

    mAboutPage = new AboutPage(mContentStack);
    mAboutPage->setObjectName(QStringLiteral("aboutPanel"));
    mContentStack->addWidget(mAboutPage);

    if (mContentHeader)
        ui->verticalLayout_3->addWidget(mContentHeader);
    ui->verticalLayout_3->addWidget(mContentStack, 1);
}

void MainWindow::showTransfersView()
{
    if (!mContentStack || !mTransfersPage)
        return;

    if (mSettingsPanel)
        mSettingsPanel->reloadFromSettings();

    mContentStack->setCurrentWidget(mTransfersPage);
    if (mContentTitle)
        mContentTitle->setText(tr("Transfers"));
    if (mActiveBadge)
        mActiveBadge->show();
    if (mHeaderSendButton)
        mHeaderSendButton->show();
    updateSidebarNavSelection(ContentView::Transfers);
    setAcceptDrops(true);
}

void MainWindow::showSettingsView()
{
    if (!mContentStack || !mSettingsPanel)
        return;

    mSettingsPanel->reloadFromSettings();
    mContentStack->setCurrentWidget(mSettingsPanel);
    if (mContentTitle)
        mContentTitle->setText(tr("Settings"));
    if (mActiveBadge)
        mActiveBadge->hide();
    if (mHeaderSendButton)
        mHeaderSendButton->hide();
    updateSidebarNavSelection(ContentView::Settings);
    setAcceptDrops(false);
}

void MainWindow::showAboutView()
{
    if (!mContentStack || !mAboutPage)
        return;

    if (mSettingsPanel)
        mSettingsPanel->reloadFromSettings();

    mContentStack->setCurrentWidget(mAboutPage);
    if (mContentTitle)
        mContentTitle->setText(tr("About"));
    if (mActiveBadge)
        mActiveBadge->hide();
    if (mHeaderSendButton)
        mHeaderSendButton->hide();
    updateSidebarNavSelection(ContentView::About);
    setAcceptDrops(false);
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

    scheduleUpdateStatusBar();
}

void MainWindow::onTransferRowsRemoved(const QModelIndex& parent, int first, int last)
{
    Q_UNUSED(parent);
    Q_UNUSED(first);
    Q_UNUSED(last);

    scheduleUpdateStatusBar();
}

int MainWindow::currentSenderRow() const
{
    return mSenderPanel ? mSenderPanel->currentRow() : -1;
}

int MainWindow::currentReceiverRow() const
{
    return mReceiverPanel ? mReceiverPanel->currentRow() : -1;
}

void MainWindow::updateActiveBadge()
{
    if (!mActiveBadge)
        return;

    const int active = countActiveTransfers(mSenderModel) + countActiveTransfers(mReceiverModel);
    mActiveBadge->setText(active > 0 ? tr("%1 active").arg(active) : tr("Idle"));
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
    updateActiveBadge();

    const int port = Settings::instance()->getTransferPort();
    const QString tls = Settings::instance()->getTlsEnabled() ? tr("TLS Active") : tr("TLS Off");
    const int uploads = countActiveTransfers(mSenderModel);
    const int downloads = countActiveTransfers(mReceiverModel);
    mStatusLabel->setText(tr("Listening on port %1 - %2 - Uploads: %3 active - Downloads: %4 active")
                              .arg(port)
                              .arg(tls)
                              .arg(uploads)
                              .arg(downloads));
}

void MainWindow::scheduleUpdateStatusBar()
{
    QTimer::singleShot(0, this, &MainWindow::updateStatusBar);
}
