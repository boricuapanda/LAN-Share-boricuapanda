#include <QtTest>
#include <QApplication>
#include <QPushButton>
#include <QSpinBox>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QTableView>

#include "settings.h"
#include "ui/mainwindow.h"
#include "ui/settingsdialog.h"
#include "ui/logviewerdialog.h"
#include "ui/tlstrustdialog.h"

class UiTest : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void mainWindowSmoke();
    void settingsDialogWidgets();
    void settingsParallelStreamsRoundTrip();
    void settingsTlsAndAuthWidgets();
    void settingsReliabilityWidgets();
    void logViewerDialogSmoke();
    void tlsTrustDialogSmoke();
};

void UiTest::initTestCase()
{
    QCoreApplication::setApplicationName("LANShareUiTest");
}

void UiTest::mainWindowSmoke()
{
    MainWindow window;
    QCOMPARE(window.windowTitle(), PROGRAM_NAME);
    QVERIFY(window.findChild<QTableView*>(QStringLiteral("senderTableView")));
    QVERIFY(window.findChild<QTableView*>(QStringLiteral("receiverTableView")));
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

void UiTest::tlsTrustDialogSmoke()
{
    TlsTrustDialog dialog;
    QCOMPARE(dialog.windowTitle(), QStringLiteral("TLS Trust Inspector"));
    QVERIFY(dialog.findChild<QListWidget*>());
}

QTEST_MAIN(UiTest)
#include "ui_test.moc"
