#include <QtTest>
#include <QComboBox>
#include <QApplication>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QTableView>
#include <QTcpServer>
#include <QTemporaryDir>
#include <QFile>
#include <QEventLoop>
#include <QTimer>
#include <QAction>
#include <QStackedWidget>
#include <QSplitter>

#include "settings.h"
#include "model/device.h"
#include "model/transferinfo.h"
#include "ui/mainwindow.h"
#include "ui/settingsdialog.h"
#include "ui/logviewerdialog.h"
#include "ui/tlstrustdialog.h"
#include "ui/receiverselectordialog.h"
#include "ui/uitheme.h"
#include "transfer/devicebroadcaster.h"
#include "model/devicelistmodel.h"

class UiTest : public QObject
{
    Q_OBJECT

    static int countSenderState(const MainWindow& window, TransferState state);

private Q_SLOTS:
    void initTestCase();
    void init();
    void ipv4MappedAddressDisplay();
    void mainWindowSmoke();
    void mainWindowInlineSettingsNavigation();
    void mainWindowEmptyStateLabels();
    void uploadQueueMaxConcurrentTransfers();
    void settingsDialogWidgets();
    void settingsParallelStreamsRoundTrip();
    void settingsThemeRoundTrip();
    void settingsTlsAndAuthWidgets();
    void settingsReliabilityWidgets();
    void logViewerDialogSmoke();
    void logViewerPhaseFilter();
    void receiverSelectorSearchField();
    void tlsTrustDialogSmoke();
};

void UiTest::initTestCase()
{
    QCoreApplication::setApplicationName("LANShareUiTest");
}

void UiTest::init()
{
    Settings::instance()->reset();
    Settings::instance()->setTlsEnabled(false);
}

void UiTest::ipv4MappedAddressDisplay()
{
    const QHostAddress mapped(QStringLiteral("::ffff:192.168.1.54"));
    QCOMPARE(Device::formatAddress(mapped), QStringLiteral("192.168.1.54"));

    Device device;
    device.setAddress(mapped);
    QCOMPARE(device.displayAddress(), QStringLiteral("192.168.1.54"));
}

int UiTest::countSenderState(const MainWindow& window, TransferState state)
{
    int count = 0;
    for (int i = 0; i < window.mSenderModel->rowCount(); ++i) {
        if (window.mSenderModel->getTransferInfo(i)->getState() == state)
            ++count;
    }
    return count;
}

void UiTest::uploadQueueMaxConcurrentTransfers()
{
    Settings* settings = Settings::instance();
    settings->setAuthEnabled(false);
    settings->setVerifyChecksum(false);
    settings->setJournalEnabled(false);
    settings->setMaxConcurrentTransfers(2);
    settings->setParallelStreams(1);

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    settings->setDownloadDir(tempDir.path());
    settings->setReplaceExistingFile(true);

    QTcpServer probe;
    QVERIFY(probe.listen(QHostAddress::LocalHost, 0));
    const quint16 port = probe.serverPort();
    probe.close();
    settings->setTransferPort(port);

    Device peer;
    peer.setAddress(QHostAddress::LocalHost);
    peer.setName(QStringLiteral("loopback"));

    QStringList paths;
    for (int i = 0; i < 3; ++i) {
        const QString path = tempDir.path() + QStringLiteral("/file%1.bin").arg(i);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(QByteArray(32 * 1024, static_cast<char>('A' + i)));
        file.close();
        paths.append(path);
    }

    MainWindow window;
    window.sendFile(QString(), paths.at(0), peer);
    window.sendFile(QString(), paths.at(1), peer);
    window.sendFile(QString(), paths.at(2), peer);

    QCOMPARE(window.mSenderModel->rowCount(), 3);
    QCOMPARE(window.activeSenderCount(), 2);
    QCOMPARE(countSenderState(window, TransferState::Queued), 1);

    QEventLoop loop;
    QTimer poll;
    poll.setInterval(50);
    QObject::connect(&poll, &QTimer::timeout, &window, [&]() {
        if (countSenderState(window, TransferState::Finish) == 3)
            loop.quit();
    });
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(30000);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    poll.start();
    timeout.start();
    loop.exec();
    poll.stop();

    QCOMPARE(countSenderState(window, TransferState::Finish), 3);
}

void UiTest::mainWindowSmoke()
{
    MainWindow window;
    QCOMPARE(window.windowTitle(), PROGRAM_NAME);
    QVERIFY(window.findChild<QWidget*>(QStringLiteral("senderTransferPanel")));
    QVERIFY(window.findChild<QWidget*>(QStringLiteral("receiverTransferPanel")));
    QVERIFY(window.findChild<QStackedWidget*>(QStringLiteral("mainContentStack")));
    QVERIFY(window.findChild<QWidget*>(QStringLiteral("settingsPanel")));
}

void UiTest::mainWindowInlineSettingsNavigation()
{
    MainWindow window;
    auto* stack = window.findChild<QStackedWidget*>(QStringLiteral("mainContentStack"));
    auto* settingsPanel = window.findChild<QWidget*>(QStringLiteral("settingsPanel"));
    auto* title = window.findChild<QLabel*>(QStringLiteral("contentTitle"));
    auto* splitter = window.findChild<QSplitter*>(QStringLiteral("splitter"));
    QVERIFY(stack);
    QVERIFY(settingsPanel);
    QVERIFY(title);
    QVERIFY(splitter);
    QCOMPARE(stack->currentWidget(), window.mTransfersPage);
    QCOMPARE(splitter->parentWidget(), window.mTransfersPage);
    QCOMPARE(title->text(), QStringLiteral("Transfers"));

    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QCoreApplication::processEvents();
    QVERIFY(settingsPanel->findChild<QCheckBox*>(QStringLiteral("overwriteCheckBox")));
    QVERIFY(settingsPanel->findChild<QCheckBox*>(QStringLiteral("tlsCheckBox")));
    QVERIFY(settingsPanel->findChild<QCheckBox*>(QStringLiteral("journalEnabledCheckBox")));

    window.mSettingsAction->trigger();
    QCOMPARE(stack->currentWidget(), settingsPanel);
    QCOMPARE(title->text(), QStringLiteral("Settings"));
    QVERIFY(window.mHeaderSendButton->isHidden());
    QVERIFY(!window.mSettingsAction->isVisible());
    QVERIFY(window.mTransfersAction->isVisible());

    auto* cancelButton = settingsPanel->findChild<QPushButton*>(QStringLiteral("pushButton_2"));
    QVERIFY(cancelButton);
    cancelButton->click();
    QCOMPARE(stack->currentWidget(), window.mTransfersPage);
    QCOMPARE(title->text(), QStringLiteral("Transfers"));
    QVERIFY(!window.mHeaderSendButton->isHidden());
    QVERIFY(window.mSettingsAction->isVisible());
    QVERIFY(!window.mTransfersAction->isVisible());

    window.mSettingsAction->trigger();
    QCOMPARE(stack->currentWidget(), settingsPanel);
    auto* saveButton = settingsPanel->findChild<QPushButton*>(QStringLiteral("pushButton"));
    QVERIFY(saveButton);
    saveButton->click();
    QCOMPARE(stack->currentWidget(), window.mTransfersPage);
    QVERIFY(window.mSettingsAction->isVisible());
    QVERIFY(!window.mTransfersAction->isVisible());
}

void UiTest::mainWindowEmptyStateLabels()
{
    MainWindow window;
    QVERIFY(window.findChild<QLabel*>(QStringLiteral("senderEmptyLabel")));
    QVERIFY(window.findChild<QLabel*>(QStringLiteral("receiverEmptyLabel")));
}

void UiTest::settingsDialogWidgets()
{
    SettingsDialog dialog;
    auto* parallelStreams = dialog.findChild<QSpinBox*>(QStringLiteral("parallelStreamsSpinBox"));
    auto* maxTransfers = dialog.findChild<QSpinBox*>(QStringLiteral("maxTransfersSpinBox"));
    auto* bufferSize = dialog.findChild<QSpinBox*>(QStringLiteral("buffSizeSpinBox"));
    auto* verifyChecksum = dialog.findChild<QCheckBox*>(QStringLiteral("verifyChecksumCheckBox"));
    auto* tls = dialog.findChild<QCheckBox*>(QStringLiteral("tlsCheckBox"));
    auto* manageTrust = dialog.findChild<QPushButton*>(QStringLiteral("manageTlsTrustBtn"));
    auto* viewLog = dialog.findChild<QPushButton*>(QStringLiteral("viewLogBtn"));

    QVERIFY(parallelStreams);
    QVERIFY(maxTransfers);
    QVERIFY(bufferSize);
    QVERIFY(verifyChecksum);
    QVERIFY(tls);
    QVERIFY(manageTrust);
    QVERIFY(viewLog);

    QCOMPARE(parallelStreams->minimum(), 1);
    QCOMPARE(parallelStreams->maximum(), 8);
    QCOMPARE(bufferSize->maximum(), 16384);
}

void UiTest::settingsParallelStreamsRoundTrip()
{
    Settings* settings = Settings::instance();
    const int previous = settings->getParallelStreams();

    SettingsDialog dialog;
    auto* parallelStreams = dialog.findChild<QSpinBox*>(QStringLiteral("parallelStreamsSpinBox"));
  auto* saveButton = dialog.findChild<QPushButton*>(QStringLiteral("pushButton"));
    QVERIFY(parallelStreams);
    QVERIFY(saveButton);

    parallelStreams->setValue(3);
    saveButton->click();
    QCOMPARE(settings->getParallelStreams(), 3);

    SettingsDialog reloaded;
    auto* reloadedSpin = reloaded.findChild<QSpinBox*>(QStringLiteral("parallelStreamsSpinBox"));
    QVERIFY(reloadedSpin);
    QCOMPARE(reloadedSpin->value(), 3);

    settings->setParallelStreams(previous);
    settings->saveSettings();
}

void UiTest::settingsThemeRoundTrip()
{
    Settings* settings = Settings::instance();
    SettingsDialog dialog;
    auto* combo = dialog.findChild<QComboBox*>(QStringLiteral("themeComboBox"));
    auto* saveButton = dialog.findChild<QPushButton*>(QStringLiteral("pushButton"));
    QVERIFY(combo);
    QVERIFY(saveButton);

    combo->setCurrentIndex(2);
    saveButton->click();
    QCOMPARE(settings->getUiTheme(), UiThemeMode::Dark);
}

void UiTest::settingsTlsAndAuthWidgets()
{
    SettingsDialog dialog;
    auto* auth = dialog.findChild<QCheckBox*>(QStringLiteral("authCheckBox"));
    auto* authToken = dialog.findChild<QLineEdit*>(QStringLiteral("authTokenLineEdit"));
    auto* tlsPinned = dialog.findChild<QLabel*>(QStringLiteral("tlsPinnedCountLabel"));
    QVERIFY(auth);
    QVERIFY(authToken);
    QVERIFY(tlsPinned);
    QVERIFY(authToken->echoMode() == QLineEdit::Password);
    QVERIFY(tlsPinned->text().contains(QStringLiteral("Pinned peers")));
}

void UiTest::settingsReliabilityWidgets()
{
    SettingsDialog dialog;
    auto* maxDownloads = dialog.findChild<QSpinBox*>(QStringLiteral("maxDownloadsSpinBox"));
    auto* retryMax = dialog.findChild<QSpinBox*>(QStringLiteral("transferRetryMaxSpinBox"));
    auto* retryBase = dialog.findChild<QSpinBox*>(QStringLiteral("transferRetryBaseSpinBox"));
    auto* journal = dialog.findChild<QCheckBox*>(QStringLiteral("journalEnabledCheckBox"));
    auto* journalRetention = dialog.findChild<QSpinBox*>(QStringLiteral("journalRetentionSpinBox"));
    auto* idleTimeout = dialog.findChild<QSpinBox*>(QStringLiteral("transferIdleTimeoutSpinBox"));
    auto* maxPacket = dialog.findChild<QSpinBox*>(QStringLiteral("maxPacketSizeSpinBox"));
    auto* offsetAckTimeout = dialog.findChild<QSpinBox*>(QStringLiteral("offsetAckTimeoutSpinBox"));

    QVERIFY(maxDownloads);
    QVERIFY(retryMax);
    QVERIFY(retryBase);
    QVERIFY(journal);
    QVERIFY(journalRetention);
    QVERIFY(idleTimeout);
    QVERIFY(maxPacket);
    QVERIFY(offsetAckTimeout);

    idleTimeout->setValue(90);
    maxPacket->setValue(8192);
    offsetAckTimeout->setValue(5);
    auto* saveButton = dialog.findChild<QPushButton*>(QStringLiteral("pushButton"));
    QVERIFY(saveButton);
    saveButton->click();

    Settings* settings = Settings::instance();
    QCOMPARE(settings->getTransferIdleTimeoutMs(), 90000);
    QCOMPARE(settings->getMaxPacketSize(), 8192 * 1024);
    QCOMPARE(settings->getTransferOffsetAckTimeoutMs(), 5000);
}

void UiTest::logViewerDialogSmoke()
{
    LogViewerDialog dialog;
    QCOMPARE(dialog.windowTitle(), QStringLiteral("Transfer Log"));
    QVERIFY(dialog.findChild<QPlainTextEdit*>());
}

void UiTest::logViewerPhaseFilter()
{
    LogViewerDialog dialog;
    QVERIFY(dialog.findChild<QComboBox*>(QStringLiteral("phaseFilterComboBox")));
}

void UiTest::receiverSelectorSearchField()
{
    DeviceBroadcaster broadcaster;
    DeviceListModel model(&broadcaster);
    ReceiverSelectorDialog dialog(&model);
    QVERIFY(dialog.findChild<QLineEdit*>(QStringLiteral("searchLineEdit")));
}

void UiTest::tlsTrustDialogSmoke()
{
    TlsTrustDialog dialog;
    QCOMPARE(dialog.windowTitle(), QStringLiteral("TLS Trust Inspector"));
    QVERIFY(dialog.findChild<QListWidget*>());
}

QTEST_MAIN(UiTest)
#include "ui_test.moc"
