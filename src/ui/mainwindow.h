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

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QLabel>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QToolButton>

#include "model/transfertablemodel.h"
#include "model/devicelistmodel.h"
#include "transfer/devicebroadcaster.h"
#include "transfer/transferserver.h"

class TransferListPanel;
class SettingsDialog;
class AboutPage;

class QDragEnterEvent;
class QDragMoveEvent;
class QDropEvent;
class QMimeData;

namespace Ui {
class MainWindow;
}

#ifdef QT_TESTLIB_LIB
class UiTest;
#endif

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    void showStartupMessage(const QString& summary);

protected:
    void closeEvent(QCloseEvent* event) override;
    void dragEnterEvent(QDragEnterEvent* event) override;
    void dragMoveEvent(QDragMoveEvent* event) override;
    void dropEvent(QDropEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

public Q_SLOTS:
    void setMainWindowVisibility(bool visible = true);

private Q_SLOTS:
    void onShowMainWindowTriggered();
    void onSendFilesActionTriggered();
    void onSendFolderActionTriggered();
    void onSettingsActionTriggered();
    void onTransfersActionTriggered();
    void onAboutActionTriggered();
    void onViewLogActionTriggered();

    void onNewReceiverAdded(Receiver* rec);

    void onSenderClearClicked();
    void onSenderCancelClicked();
    void onSenderPauseClicked();
    void onSenderResumeClicked();

    void onReceiverClearClicked();
    void onReceiverCancelClicked();
    void onReceiverPauseClicked();
    void onReceiverResumeClicked();

    void onSenderPanelSelectionChanged(int row);
    void onReceiverPanelSelectionChanged(int row);
    void onSenderPanelActivated(int row);
    void onReceiverPanelActivated(int row);
    void onSenderPanelContextMenu(const QPoint& globalPos, int row);
    void onReceiverPanelContextMenu(const QPoint& globalPos, int row);

    void openSenderFileInCurrentIndex();
    void openSenderFolderInCurrentIndex();
    void removeSenderItemInCurrentIndex();

    void openReceiverFileInCurrentIndex();
    void openReceiverFolderInCurrentIndex();
    void removeReceiverItemInCurrentIndex();
    void deleteReceiverFileInCurrentIndex();

    void onSelectedSenderStateChanged(TransferState state);
    void onSelectedReceiverStateChanged(TransferState state);

    void quitApp();

private:
    void setupActions();
    void setupToolbar();
    void setupSystrayIcon();
    void connectSignals();
    void sendFile(const QString& folderName, const QString& fileName, const Device& receiver);
    void selectReceiversAndSendTheFiles(QVector<QPair<QString, QString> > dirNameAndFullPath);
    static bool mimeHasLocalUrls(const QMimeData* mimeData);
    void processSendQueue();
    int activeSenderCount() const;
    void connectSenderQueueSignals(TransferInfo* info);
    void setupTransferPanels();
    void setupContentStack();
    void showTransfersView();
    void showSettingsView();
    void showAboutView();
    enum class ContentView { Transfers, Settings, About };
    void updateSidebarNavSelection(ContentView activeView);
    QToolButton* createSidebarNavButton(const QString& text, const QIcon& icon, bool checkable);
    void setupContentHeader();
    void setupSidebarBranding();
    void setupAccessibility();
    void connectTransferModelSignals();
    void updateStatusBar();
    void scheduleUpdateStatusBar();
    void onTransferRowsInserted(const QModelIndex& parent, int first, int last);
    void onTransferRowsRemoved(const QModelIndex& parent, int first, int last);
    static int countActiveTransfers(const TransferTableModel* model);
    int currentSenderRow() const;
    int currentReceiverRow() const;
    void updateActiveBadge();

#ifdef QT_TESTLIB_LIB
    friend class UiTest;
#endif

    bool mForceQuit{false};
    Ui::MainWindow *ui;
    QSystemTrayIcon* mSystrayIcon;
    QMenu* mSystrayMenu;

    TransferTableModel* mSenderModel;
    TransferTableModel* mReceiverModel;
    DeviceListModel* mDeviceModel;

    DeviceBroadcaster* mBroadcaster;
    TransferServer* mTransServer;

    TransferListPanel* mSenderPanel{nullptr};
    TransferListPanel* mReceiverPanel{nullptr};
    SettingsDialog* mSettingsPanel{nullptr};
    AboutPage* mAboutPage{nullptr};
    QStackedWidget* mContentStack{nullptr};
    QWidget* mTransfersPage{nullptr};
    QWidget* mContentHeader{nullptr};
    QLabel* mContentTitle{nullptr};
    QToolButton* mNavTransfersBtn{nullptr};
    QToolButton* mNavSettingsBtn{nullptr};
    QToolButton* mNavAboutBtn{nullptr};
    QLabel* mStatusLabel{nullptr};
    QLabel* mActiveBadge{nullptr};
    QWidget* mHeaderSendButton{nullptr};

    QAction* mShowMainWindowAction;
    QAction* mSendFilesAction;
    QAction* mSendFolderAction;
    QAction* mSettingsAction;
    QAction* mTransfersAction;
    QAction* mViewLogAction;
    QAction* mAboutAction;
    QAction* mAboutQtAction;
    QAction* mQuitAction;

    QAction* mSenderOpenAction;
    QAction* mSenderOpenFolderAction;
    QAction* mSenderRemoveAction;
    QAction* mSenderClearAction;
    QAction* mSenderPauseAction;
    QAction* mSenderResumeAction;
    QAction* mSenderCancelAction;

    QAction* mRecOpenAction;
    QAction* mRecOpenFolderAction;
    QAction* mRecRemoveAction;
    QAction* mRecDeleteAction;
    QAction* mRecClearAction;
    QAction* mRecPauseAction;
    QAction* mRecResumeAction;
    QAction* mRecCancelAction;
};

#endif // MAINWINDOW_H
