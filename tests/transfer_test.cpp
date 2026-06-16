#include <QtTest>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QFile>
#include <QEventLoop>
#include <QTimer>
#include <QElapsedTimer>
#include <climits>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QRandomGenerator>
#include <cmath>
#include <memory>

#include "settings.h"
#include "util.h"
#include "log.h"
#include "transfer/sender.h"
#include "transfer/receiver.h"
#include "transfer/transfer.h"
#include "transfer/transferjournal.h"
#include "transfer/tlshelper.h"
#include "transfer/transferserver.h"
#include "transfer/devicebroadcaster.h"
#include "model/devicelistmodel.h"
#include "model/device.h"
#include "model/transferfailure.h"

class TransferTest : public QObject
{
    Q_OBJECT

    std::unique_ptr<QTemporaryDir> mJournalTempDir;

private Q_SLOTS:
    void initTestCase();
    void init();
    void checksumVerification();
    void authenticationRequiredRejectsNoToken();
    void verifyRequiredRejectsSenderDisable();
    void checksumFailure();
    void insufficientDiskSpace();
    void resumePartialDownload();
    void parallelStreamsSettingFallback();
    void parallelStreamsTwoSocketChecksum();
    void parallelStreamsCancelPath();
    void parallelStreamsPressureSoak();
    void parallelStreamsMaxEightSockets();
    void parallelStreamsConcurrentBurst();
    void legacyReceiverOffsetAckTimeout();
    void legacySenderWithoutVerifyAccepted();
    void receiverOpenFailureCancelsSender();
    void batch10_rejectsOversizedPacket();
    void batch10_rejectsInvalidPacketType();
    void batch10_journalRoundTrip();
    void batch10_journalFingerprintMismatchResetsPart();
    void batch10_admissionBusyOffsetAckFailsSender();
    void batch10_soakRepeatedTransfers();
    void batch10_uploadRetryAfterDisconnect();
    void interop_legacyMinimalOffsetAck();
    void batch10_journalCrashRecoveryStartup();
    void batch10_journalChurnBurst();
    void batch10_randomizedInterruptionCampaign();
    void batch10_chunkBoundaryResumeIntegrity();
    void batch10_soakShipGateSlo();
    void batch10_transferLogStatsParsing();
    void gap_transferServerAdmissionRetrySuccess();
    void gap_tlsLoopbackTransfer();
    void gap_concurrentMultiSender();
};

static void writePacket(QTcpSocket& socket, qint32 packetDataSize, PacketType type, const QByteArray& data)
{
    socket.write(reinterpret_cast<const char*>(&packetDataSize), sizeof(packetDataSize));
    const char packetType = static_cast<char>(type);
    socket.write(&packetType, sizeof(packetType));
    socket.write(data);
}

void TransferTest::initTestCase()
{
    QCoreApplication::setApplicationName("LANShareTransferTest");
    Settings::instance()->reset();
    Settings::instance()->setTlsEnabled(false);
    mJournalTempDir = std::make_unique<QTemporaryDir>();
    QVERIFY(mJournalTempDir->isValid());
    TransferJournal::setStoragePathForTests(mJournalTempDir->path() + "/transfer-journal.json");
}

void TransferTest::init()
{
    Settings::instance()->reset();
    Settings::instance()->setTlsEnabled(false);

    TransferJournal* journal = TransferJournal::instance();
    const QList<JournalEntry> entries = journal->loadAll();
    for (const JournalEntry& entry : entries)
        journal->remove(entry.transferId);

    TlsHelper::clearTlsConfigDirForTests();
    TlsHelper::clearPinnedPeers();
}

static quint16 reserveLocalTransferPort()
{
    QTcpServer probe;
    if (!probe.listen(QHostAddress::LocalHost, 0))
        return 0;
    const quint16 port = probe.serverPort();
    probe.close();
    return port;
}

static Device makeLocalPeerDevice()
{
    Device peer;
    peer.setAddress(QHostAddress::LocalHost);
    peer.setName(QStringLiteral("test-peer"));
    return peer;
}

static bool startTransferServer(TransferServer* server, DeviceListModel* devList)
{
    const quint16 port = reserveLocalTransferPort();
    if (port == 0)
        return false;

    Settings::instance()->setTransferPort(port);
    Device peer = makeLocalPeerDevice();
    devList->setDevices({peer});
    return server->listen(QHostAddress::LocalHost);
}

static TransferState runTransfer(const QString& sourcePath,
                                 const QString& downloadDir,
                                 bool verifyChecksum,
                                 bool resumePartial,
                                 int parallelStreams = 1)
{
    Settings* settings = Settings::instance();
    settings->setDownloadDir(downloadDir);
    settings->setVerifyChecksum(verifyChecksum);
    settings->setResumePartialDownloads(resumePartial);
    settings->setReplaceExistingFile(true);
    settings->setAuthEnabled(false);
    settings->setAuthToken(QString());
    settings->setTlsEnabled(false);
    settings->setParallelStreams(parallelStreams);

    QTcpServer server;
    if (!server.listen(QHostAddress::LocalHost, 0))
        return TransferState::Failed;
    settings->setTransferPort(server.serverPort());

    Device localDevice;
    localDevice.setAddress(QHostAddress::LocalHost);
    localDevice.setName("test-peer");

    Receiver* receiver = nullptr;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(15000);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);

    QObject::connect(&server, &QTcpServer::newConnection, [&]() {
        QTcpSocket* socket = server.nextPendingConnection();
        Receiver* created = new Receiver(localDevice, socket);
        if (!receiver) {
            receiver = created;
            QObject::connect(receiver->getTransferInfo(), &TransferInfo::done, &loop, &QEventLoop::quit);
            QObject::connect(receiver->getTransferInfo(), &TransferInfo::errorOcurred, &loop, &QEventLoop::quit);
        }
    });

    Sender sender(localDevice, QString(), sourcePath);
    if (!sender.start()) {
        timeout.stop();
        return TransferState::Failed;
    }

    timeout.start();
    loop.exec();
    timeout.stop();

    if (!receiver)
        return TransferState::Failed;

    return receiver->getTransferInfo()->getState();
}

static bool isFileAbsentOrChecksumValid(const QString& path, const QString& sourcePath)
{
    if (!QFile::exists(path))
        return true;
    if (QFileInfo(path).size() == 0)
        return true;
    if (QFileInfo(path).size() != QFileInfo(sourcePath).size())
        return false;
    return Util::fileSha256(path) == Util::fileSha256(sourcePath);
}

void TransferTest::checksumVerification()
{
    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString downloadDir = tempRoot.path() + "/downloads";
    QVERIFY(QDir().mkpath(downloadDir));

    const QString sourcePath = tempRoot.path() + "/source.bin";
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));

    QByteArray payload;
    payload.resize(256 * 1024);
    for (int i = 0; i < payload.size(); ++i)
        payload[i] = char(i & 0xff);
    QCOMPARE(source.write(payload), payload.size());
    source.close();

    const TransferState state = runTransfer(sourcePath, downloadDir, true, false, 1);
    QCOMPARE(state, TransferState::Finish);

    const QString expectedPath = downloadDir + "/source.bin";
    QVERIFY(QFile::exists(expectedPath));
    QCOMPARE(Util::fileSha256(expectedPath), Util::fileSha256(sourcePath));
}

void TransferTest::authenticationRequiredRejectsNoToken()
{
    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString downloadDir = tempRoot.path() + "/downloads";
    QVERIFY(QDir().mkpath(downloadDir));

    Settings* settings = Settings::instance();
    settings->setDownloadDir(downloadDir);
    settings->setVerifyChecksum(false);
    settings->setResumePartialDownloads(false);
    settings->setReplaceExistingFile(true);
    settings->setAuthEnabled(true);
    settings->setAuthToken("expected-token");
    settings->setTlsEnabled(false);

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    settings->setTransferPort(server.serverPort());

    Device localDevice;
    localDevice.setAddress(QHostAddress::LocalHost);
    localDevice.setName("test-peer");

    Receiver* receiver = nullptr;
    QObject::connect(&server, &QTcpServer::newConnection, [&]() {
        receiver = new Receiver(localDevice, server.nextPendingConnection());
    });

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, settings->getTransferPort());
    QVERIFY(client.waitForConnected(3000));
    QTRY_VERIFY(receiver != nullptr);

    const QJsonObject header({
        {"name", "auth.bin"},
        {"folder", ""},
        {"size", 16},
        {"verify", false},
        {"auth", false}
    });
    const QByteArray headerData = QJsonDocument(header).toJson(QJsonDocument::Compact);
    writePacket(client, headerData.size(), PacketType::Header, headerData);
    QVERIFY(client.waitForBytesWritten(3000));

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(5000);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(receiver->getTransferInfo(), &TransferInfo::errorOcurred, &loop, &QEventLoop::quit);
    timeout.start();
    loop.exec();

    QCOMPARE(receiver->getTransferInfo()->getState(), TransferState::Failed);
    settings->setAuthEnabled(false);
    settings->setAuthToken(QString());
}

void TransferTest::verifyRequiredRejectsSenderDisable()
{
    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString downloadDir = tempRoot.path() + "/downloads";
    QVERIFY(QDir().mkpath(downloadDir));

    Settings* settings = Settings::instance();
    settings->setDownloadDir(downloadDir);
    settings->setVerifyChecksum(true);
    settings->setResumePartialDownloads(false);
    settings->setReplaceExistingFile(true);
    settings->setAuthEnabled(false);
    settings->setTlsEnabled(false);

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    settings->setTransferPort(server.serverPort());

    Device localDevice;
    localDevice.setAddress(QHostAddress::LocalHost);
    localDevice.setName("test-peer");

    Receiver* receiver = nullptr;
    QObject::connect(&server, &QTcpServer::newConnection, [&]() {
        receiver = new Receiver(localDevice, server.nextPendingConnection());
    });

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, settings->getTransferPort());
    QVERIFY(client.waitForConnected(3000));
    QTRY_VERIFY(receiver != nullptr);

    const QJsonObject header({
        {"name", "verify.bin"},
        {"folder", ""},
        {"size", 32},
        {"verify", false}
    });
    const QByteArray headerData = QJsonDocument(header).toJson(QJsonDocument::Compact);
    writePacket(client, headerData.size(), PacketType::Header, headerData);
    QVERIFY(client.waitForBytesWritten(3000));

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(5000);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(receiver->getTransferInfo(), &TransferInfo::errorOcurred, &loop, &QEventLoop::quit);
    timeout.start();
    loop.exec();

    QCOMPARE(receiver->getTransferInfo()->getState(), TransferState::Failed);
}

void TransferTest::checksumFailure()
{
    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString downloadDir = tempRoot.path() + "/downloads";
    QVERIFY(QDir().mkpath(downloadDir));

    Settings* settings = Settings::instance();
    settings->setDownloadDir(downloadDir);
    settings->setVerifyChecksum(true);
    settings->setResumePartialDownloads(true);
    settings->setReplaceExistingFile(true);
    settings->setTlsEnabled(false);

    const QByteArray payload = QByteArray("checksum failure should reject this file");
    const QString expectedPath = downloadDir + "/bad.bin";
    const QString partPath = expectedPath + ".part";

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    settings->setTransferPort(server.serverPort());

    Device localDevice;
    localDevice.setAddress(QHostAddress::LocalHost);
    localDevice.setName("test-peer");

    Receiver* receiver = nullptr;
    QObject::connect(&server, &QTcpServer::newConnection, [&]() {
        receiver = new Receiver(localDevice, server.nextPendingConnection());
    });

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, settings->getTransferPort());
    QVERIFY(client.waitForConnected(3000));
    QTRY_VERIFY(receiver != nullptr);

    const QJsonObject header({
        {"name", "bad.bin"},
        {"folder", ""},
        {"size", payload.size()},
        {"verify", true}
    });
    const QByteArray headerData = QJsonDocument(header).toJson(QJsonDocument::Compact);
    writePacket(client, headerData.size(), PacketType::Header, headerData);
    QVERIFY(client.waitForBytesWritten(3000));

    QElapsedTimer headerTimer;
    headerTimer.start();
    while (receiver->getTransferInfo()->getState() != TransferState::Transfering
           && headerTimer.elapsed() < 3000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    QCOMPARE(receiver->getTransferInfo()->getState(), TransferState::Transfering);

    writePacket(client, payload.size(), PacketType::Data, payload);
    QVERIFY(client.waitForBytesWritten(3000));

    const QJsonObject finish({{"sha256", QStringLiteral("deadbeef")}});
    const QByteArray finishData = QJsonDocument(finish).toJson(QJsonDocument::Compact);
    writePacket(client, finishData.size(), PacketType::Finish, finishData);
    QVERIFY(client.waitForBytesWritten(3000));

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(5000);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(receiver->getTransferInfo(), &TransferInfo::errorOcurred, &loop, &QEventLoop::quit);
    timeout.start();
    loop.exec();

    QCOMPARE(receiver->getTransferInfo()->getState(), TransferState::Failed);
    QVERIFY(!QFile::exists(expectedPath));
    QVERIFY(!QFile::exists(partPath));
}

void TransferTest::insufficientDiskSpace()
{
    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString downloadDir = tempRoot.path() + "/downloads";
    QVERIFY(QDir().mkpath(downloadDir));

    Settings* settings = Settings::instance();
    settings->setDownloadDir(downloadDir);
    settings->setVerifyChecksum(false);
    settings->setResumePartialDownloads(false);
    settings->setReplaceExistingFile(true);
    settings->setTlsEnabled(false);

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    settings->setTransferPort(server.serverPort());

    Device localDevice;
    localDevice.setAddress(QHostAddress::LocalHost);
    localDevice.setName("test-peer");

    Receiver* receiver = nullptr;
    QObject::connect(&server, &QTcpServer::newConnection, [&]() {
        receiver = new Receiver(localDevice, server.nextPendingConnection());
    });

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, settings->getTransferPort());
    QVERIFY(client.waitForConnected(3000));
    QTRY_VERIFY(receiver != nullptr);

    const QJsonObject header({
        {"name", "huge.bin"},
        {"folder", ""},
        {"size", LLONG_MAX / 2},
        {"verify", false}
    });
    const QByteArray headerData = QJsonDocument(header).toJson(QJsonDocument::Compact);
    writePacket(client, headerData.size(), PacketType::Header, headerData);
    QVERIFY(client.waitForBytesWritten(3000));

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(5000);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(receiver->getTransferInfo(), &TransferInfo::errorOcurred, &loop, &QEventLoop::quit);
    timeout.start();
    loop.exec();

    QCOMPARE(receiver->getTransferInfo()->getState(), TransferState::Failed);
    QVERIFY(!QFile::exists(downloadDir + "/huge.bin"));
}

void TransferTest::resumePartialDownload()
{
    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString downloadDir = tempRoot.path() + "/downloads";
    QVERIFY(QDir().mkpath(downloadDir));

    const QString sourcePath = tempRoot.path() + "/resume.bin";
    const QByteArray payload =
        QByteArray("LANShare resume test payload for partial download verification.");
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(payload), payload.size());
    source.close();

    const QString partPath = downloadDir + "/resume.bin.part";
    QFile part(partPath);
    QVERIFY(part.open(QIODevice::WriteOnly));
    const QByteArray partial = payload.left(payload.size() / 2);
    QCOMPARE(part.write(partial), partial.size());
    part.close();

    const TransferState state = runTransfer(sourcePath, downloadDir, true, true, 1);
    QCOMPARE(state, TransferState::Finish);

    const QString expectedPath = downloadDir + "/resume.bin";
    QVERIFY(QFile::exists(expectedPath));
    QVERIFY(!QFile::exists(partPath));

    QFile out(expectedPath);
    QVERIFY(out.open(QIODevice::ReadOnly));
    QCOMPARE(out.readAll(), payload);
    QCOMPARE(Util::fileSha256(expectedPath), Util::fileSha256(sourcePath));
}

void TransferTest::parallelStreamsSettingFallback()
{
    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString downloadDir = tempRoot.path() + "/downloads";
    QVERIFY(QDir().mkpath(downloadDir));

    const QString sourcePath = tempRoot.path() + "/parallel.bin";
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    const QByteArray payload(128 * 1024, 'p');
    QCOMPARE(source.write(payload), payload.size());
    source.close();

    const QString partPath = downloadDir + "/parallel.bin.part";
    QFile part(partPath);
    QVERIFY(part.open(QIODevice::WriteOnly));
    const QByteArray partial = payload.left(payload.size() / 2);
    QCOMPARE(part.write(partial), partial.size());
    part.close();

    const TransferState state = runTransfer(sourcePath, downloadDir, true, true, 2);
    QCOMPARE(state, TransferState::Finish);

    const QString expectedPath = downloadDir + "/parallel.bin";
    QVERIFY(QFile::exists(expectedPath));
    QCOMPARE(Util::fileSha256(expectedPath), Util::fileSha256(sourcePath));
}

void TransferTest::parallelStreamsTwoSocketChecksum()
{
    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString downloadDir = tempRoot.path() + "/downloads";
    QVERIFY(QDir().mkpath(downloadDir));

    const QString sourcePath = tempRoot.path() + "/parallel-two.bin";
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    const QByteArray payload(512 * 1024, 'm');
    QCOMPARE(source.write(payload), payload.size());
    source.close();

    const TransferState state = runTransfer(sourcePath, downloadDir, true, false, 2);
    QCOMPARE(state, TransferState::Finish);

    const QString expectedPath = downloadDir + "/parallel-two.bin";
    QVERIFY(QFile::exists(expectedPath));
    QCOMPARE(Util::fileSha256(expectedPath), Util::fileSha256(sourcePath));
}

void TransferTest::parallelStreamsCancelPath()
{
    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString downloadDir = tempRoot.path() + "/downloads";
    QVERIFY(QDir().mkpath(downloadDir));

    Settings* settings = Settings::instance();
    settings->setDownloadDir(downloadDir);
    settings->setVerifyChecksum(true);
    settings->setResumePartialDownloads(false);
    settings->setReplaceExistingFile(true);
    settings->setAuthEnabled(false);
    settings->setAuthToken(QString());
    settings->setTlsEnabled(false);
    settings->setParallelStreams(2);
    settings->setFileBufferSize(64 * 1024);

    const QString sourcePath = tempRoot.path() + "/cancel.bin";
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    const QByteArray payload(32 * 1024 * 1024, 'c');
    QCOMPARE(source.write(payload), payload.size());
    source.close();

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    settings->setTransferPort(server.serverPort());

    Device localDevice;
    localDevice.setAddress(QHostAddress::LocalHost);
    localDevice.setName("test-peer");

    Receiver* receiver = nullptr;
    QObject::connect(&server, &QTcpServer::newConnection, [&]() {
        QTcpSocket* socket = server.nextPendingConnection();
        Receiver* created = new Receiver(localDevice, socket);
        if (!receiver) {
            receiver = created;
            QObject::connect(receiver->getTransferInfo(), &TransferInfo::fileOpened, receiver, [receiver]() {
                QTimer::singleShot(1, receiver, &Receiver::cancel);
            });
        }
    });

    Sender sender(localDevice, QString(), sourcePath);
    QVERIFY(sender.start());

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 8000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (receiver && receiver->getTransferInfo()->getState() == TransferState::Cancelled)
            break;
    }

    QVERIFY(receiver != nullptr);
    const TransferState endState = receiver->getTransferInfo()->getState();
    QVERIFY(endState == TransferState::Cancelled ||
            endState == TransferState::Failed ||
            endState == TransferState::Disconnected);
}

void TransferTest::parallelStreamsPressureSoak()
{
    bool ok = false;
    int iterations = qEnvironmentVariableIntValue("LANSHARE_PARALLEL_SOAK_ITERATIONS", &ok);
    if (!ok || iterations <= 0)
        iterations = 25;

    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString downloadBase = tempRoot.path() + "/parallel-soak";
    QVERIFY(QDir().mkpath(downloadBase));

    int successes = 0;
    for (int i = 0; i < iterations; ++i) {
        const int streams = (i % 2 == 0) ? 2 : 4;
        const int sizeKiB = 128 + (i % 5) * 128;

        const QString sourcePath = tempRoot.path() + QStringLiteral("/soak-%1.bin").arg(i);
        QFile source(sourcePath);
        QVERIFY(source.open(QIODevice::WriteOnly));
        QByteArray payload(sizeKiB * 1024, static_cast<char>('p' + (i % 8)));
        for (int b = 0; b < payload.size(); ++b)
            payload[b] = static_cast<char>((b + i * 13) & 0xff);
        QCOMPARE(source.write(payload), payload.size());
        source.close();

        const QString downloadDir = downloadBase + QStringLiteral("/iter-%1").arg(i);
        QVERIFY(QDir().mkpath(downloadDir));

        const TransferState state = runTransfer(sourcePath, downloadDir, true, false, streams);
        const QString expectedPath = downloadDir + QStringLiteral("/soak-%1.bin").arg(i);
        if (state == TransferState::Finish
            && QFile::exists(expectedPath)
            && Util::fileSha256(expectedPath) == Util::fileSha256(sourcePath)) {
            ++successes;
        }
    }

    const int maxFailures = static_cast<int>(std::floor(iterations * 0.005));
    const int required = iterations - maxFailures;
    QVERIFY2(successes >= required,
             qPrintable(QStringLiteral("parallel soak SLO miss: %1/%2 (need >= %3)")
                            .arg(successes)
                            .arg(iterations)
                            .arg(required)));
}

void TransferTest::parallelStreamsMaxEightSockets()
{
    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString downloadDir = tempRoot.path() + "/downloads";
    QVERIFY(QDir().mkpath(downloadDir));

    const QString sourcePath = tempRoot.path() + "/parallel-eight.bin";
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QByteArray payload(4 * 1024 * 1024, '8');
    for (int i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<char>((i * 11) & 0xff);
    QCOMPARE(source.write(payload), payload.size());
    source.close();

    Settings* settings = Settings::instance();
    settings->setFileBufferSize(256 * 1024);

    const TransferState state = runTransfer(sourcePath, downloadDir, true, false, 8);
    QCOMPARE(state, TransferState::Finish);

    const QString expectedPath = downloadDir + "/parallel-eight.bin";
    QVERIFY(QFile::exists(expectedPath));
    QCOMPARE(Util::fileSha256(expectedPath), Util::fileSha256(sourcePath));
}

void TransferTest::parallelStreamsConcurrentBurst()
{
    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString downloadBase = tempRoot.path() + "/burst";
    QVERIFY(QDir().mkpath(downloadBase));

    const QString sourcePath = tempRoot.path() + "/burst.bin";
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    const QByteArray payload(768 * 1024, 'b');
    QCOMPARE(source.write(payload), payload.size());
    source.close();

    const int bursts = 8;
    int successes = 0;
    for (int i = 0; i < bursts; ++i) {
        const QString downloadDir = downloadBase + QStringLiteral("/b-%1").arg(i);
        QVERIFY(QDir().mkpath(downloadDir));
        const TransferState state = runTransfer(sourcePath, downloadDir, true, false, 2);
        const QString expectedPath = downloadDir + "/burst.bin";
        if (state == TransferState::Finish
            && Util::fileSha256(expectedPath) == Util::fileSha256(sourcePath)) {
            ++successes;
        }
    }

    QCOMPARE(successes, bursts);
}

void TransferTest::legacyReceiverOffsetAckTimeout()
{
    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString downloadDir = tempRoot.path() + "/downloads";
    QVERIFY(QDir().mkpath(downloadDir));

    const QString sourcePath = tempRoot.path() + "/legacy.bin";
    const QByteArray payload = QByteArray("legacy receiver offset-ack timeout fallback");
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(payload), payload.size());
    source.close();

    Settings* settings = Settings::instance();
    settings->setDownloadDir(downloadDir);
    settings->setVerifyChecksum(false);
    settings->setResumePartialDownloads(false);
    settings->setReplaceExistingFile(true);
    settings->setTlsEnabled(false);
    settings->setParallelStreams(1);

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    settings->setTransferPort(server.serverPort());

    Device localDevice;
    localDevice.setAddress(QHostAddress::LocalHost);
    localDevice.setName("test-peer");

    QString outputPath;
    QObject::connect(&server, &QTcpServer::newConnection, [&]() {
        QTcpSocket* socket = server.nextPendingConnection();
        outputPath = downloadDir + "/legacy.bin";
        QTimer::singleShot(0, [socket, payload, outputPath]() {
            QFile out(outputPath);
            if (!out.open(QIODevice::WriteOnly))
                return;

            auto readPacket = [socket](PacketType& typeOut, QByteArray& payloadOut) -> bool {
                qint32 packetSize = -1;
                QElapsedTimer waitTimer;
                waitTimer.start();
                while (socket->bytesAvailable() < static_cast<int>(sizeof(qint32)) && waitTimer.elapsed() < 5000) {
                    socket->waitForReadyRead(50);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                }
                if (socket->bytesAvailable() < static_cast<int>(sizeof(qint32)))
                    return false;
                socket->read(reinterpret_cast<char*>(&packetSize), sizeof(packetSize));
                waitTimer.restart();
                while (socket->bytesAvailable() < packetSize + 1 && waitTimer.elapsed() < 5000) {
                    socket->waitForReadyRead(50);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                }
                if (socket->bytesAvailable() < packetSize + 1)
                    return false;
                typeOut = static_cast<PacketType>(socket->read(1).at(0));
                payloadOut = socket->read(packetSize);
                return true;
            };

            PacketType type = PacketType::Header;
            QByteArray packetPayload;
            if (!readPacket(type, packetPayload) || type != PacketType::Header)
                return;

            while (out.size() < payload.size()) {
                if (!readPacket(type, packetPayload))
                    continue;
                if (type == PacketType::Data)
                    out.write(packetPayload);
                else if (type == PacketType::Finish)
                    break;
            }
            out.close();
        });
    });

    Sender sender(localDevice, QString(), sourcePath);
    QVERIFY(sender.start());

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (sender.getTransferInfo()->getState() == TransferState::Finish)
            break;
    }

    QCOMPARE(sender.getTransferInfo()->getState(), TransferState::Finish);
    QVERIFY(QFile::exists(outputPath));
    QFile out(outputPath);
    QVERIFY(out.open(QIODevice::ReadOnly));
    QCOMPARE(out.readAll(), payload);
}

void TransferTest::legacySenderWithoutVerifyAccepted()
{
    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString downloadDir = tempRoot.path() + "/downloads";
    QVERIFY(QDir().mkpath(downloadDir));

    Settings* settings = Settings::instance();
    settings->setDownloadDir(downloadDir);
    settings->setVerifyChecksum(true);
    settings->setResumePartialDownloads(false);
    settings->setReplaceExistingFile(true);
    settings->setTlsEnabled(false);

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    settings->setTransferPort(server.serverPort());

    Device localDevice;
    localDevice.setAddress(QHostAddress::LocalHost);
    localDevice.setName("test-peer");

    Receiver* receiver = nullptr;
    QObject::connect(&server, &QTcpServer::newConnection, [&]() {
        receiver = new Receiver(localDevice, server.nextPendingConnection());
    });

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, settings->getTransferPort());
    QVERIFY(client.waitForConnected(3000));
    QTRY_VERIFY(receiver != nullptr);

    const QByteArray payload = QByteArray("legacy sender without verify field");
    const QJsonObject header({
        {"name", "legacy-verify.bin"},
        {"folder", ""},
        {"size", payload.size()}
    });
    const QByteArray headerData = QJsonDocument(header).toJson(QJsonDocument::Compact);
    writePacket(client, headerData.size(), PacketType::Header, headerData);
    QVERIFY(client.waitForBytesWritten(3000));

    QElapsedTimer headerTimer;
    headerTimer.start();
    while (receiver->getTransferInfo()->getState() != TransferState::Transfering
           && headerTimer.elapsed() < 3000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    }
    QCOMPARE(receiver->getTransferInfo()->getState(), TransferState::Transfering);

    writePacket(client, payload.size(), PacketType::Data, payload);
    QVERIFY(client.waitForBytesWritten(3000));
    writePacket(client, 0, PacketType::Finish, QByteArray());
    QVERIFY(client.waitForBytesWritten(3000));

    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(5000);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(receiver->getTransferInfo(), &TransferInfo::done, &loop, &QEventLoop::quit);
    timeout.start();
    loop.exec();

    QCOMPARE(receiver->getTransferInfo()->getState(), TransferState::Finish);
    const QString expectedPath = downloadDir + "/legacy-verify.bin";
    QVERIFY(QFile::exists(expectedPath));
    QFile out(expectedPath);
    QVERIFY(out.open(QIODevice::ReadOnly));
    QCOMPARE(out.readAll(), payload);
}

void TransferTest::receiverOpenFailureCancelsSender()
{
    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString blockedDir = tempRoot.path() + "/blocked";
    QVERIFY(QDir().mkpath(blockedDir));
    QFile::setPermissions(blockedDir, QFile::ReadOwner | QFile::ExeOwner);

    Settings* settings = Settings::instance();
    settings->setDownloadDir(blockedDir);
    settings->setVerifyChecksum(false);
    settings->setResumePartialDownloads(false);
    settings->setReplaceExistingFile(true);
    settings->setTlsEnabled(false);

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    settings->setTransferPort(server.serverPort());

    Device localDevice;
    localDevice.setAddress(QHostAddress::LocalHost);
    localDevice.setName("test-peer");

    Receiver* receiver = nullptr;
    QObject::connect(&server, &QTcpServer::newConnection, [&]() {
        receiver = new Receiver(localDevice, server.nextPendingConnection());
    });

    const QString sourcePath = tempRoot.path() + "/blocked-source.bin";
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(QByteArray("blocked")), qint64(7));
    source.close();

    Sender sender(localDevice, QString(), sourcePath);
    QVERIFY(sender.start());

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 8000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        const TransferState senderState = sender.getTransferInfo()->getState();
        if (senderState == TransferState::Failed ||
            senderState == TransferState::Cancelled ||
            senderState == TransferState::Disconnected) {
            break;
        }
    }

    QVERIFY(receiver != nullptr);
    QCOMPARE(receiver->getTransferInfo()->getState(), TransferState::Failed);
    const TransferState senderState = sender.getTransferInfo()->getState();
    QVERIFY(senderState == TransferState::Failed ||
            senderState == TransferState::Cancelled ||
            senderState == TransferState::Disconnected);

    QFile::setPermissions(blockedDir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
}

void TransferTest::batch10_rejectsOversizedPacket()
{
    Settings* settings = Settings::instance();
    settings->setTlsEnabled(false);
    settings->setMaxPacketSize(1024);

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    Device localDevice;
    localDevice.setAddress(QHostAddress::LocalHost);
    localDevice.setName("test-peer");

    Receiver* receiver = nullptr;
    QObject::connect(&server, &QTcpServer::newConnection, [&]() {
        receiver = new Receiver(localDevice, server.nextPendingConnection());
    });

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(client.waitForConnected(3000));
    QTRY_VERIFY(receiver != nullptr);

    const qint32 hugeSize = settings->getMaxPacketSize() + 1;
    const QByteArray payload(hugeSize, 'x');
    writePacket(client, hugeSize, PacketType::Data, payload);
    QVERIFY(client.waitForBytesWritten(3000));

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (receiver->getTransferInfo()->getState() == TransferState::Failed)
            break;
    }

    QCOMPARE(receiver->getTransferInfo()->getState(), TransferState::Failed);
    QCOMPARE(receiver->getTransferInfo()->getFailureReason(), TransferFailureReason::PacketOversize);
    settings->setMaxPacketSize(20 * 1024 * 1024);
}

void TransferTest::batch10_rejectsInvalidPacketType()
{
    Settings* settings = Settings::instance();
    settings->setTlsEnabled(false);

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));

    Device localDevice;
    localDevice.setAddress(QHostAddress::LocalHost);
    localDevice.setName("test-peer");

    Receiver* receiver = nullptr;
    QObject::connect(&server, &QTcpServer::newConnection, [&]() {
        receiver = new Receiver(localDevice, server.nextPendingConnection());
    });

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, server.serverPort());
    QVERIFY(client.waitForConnected(3000));
    QTRY_VERIFY(receiver != nullptr);

    const qint32 packetSize = 4;
    const QByteArray payload = QByteArray("test");
    const char invalidType = static_cast<char>(0x7f);
    client.write(reinterpret_cast<const char*>(&packetSize), sizeof(packetSize));
    client.write(&invalidType, sizeof(invalidType));
    client.write(payload);
    QVERIFY(client.waitForBytesWritten(3000));

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (receiver->getTransferInfo()->getState() == TransferState::Failed)
            break;
    }

    QCOMPARE(receiver->getTransferInfo()->getState(), TransferState::Failed);
    QCOMPARE(receiver->getTransferInfo()->getFailureReason(), TransferFailureReason::InvalidPacketType);
}

void TransferTest::batch10_journalRoundTrip()
{
    Settings* settings = Settings::instance();
    settings->setJournalEnabled(true);

    TransferJournal* journal = TransferJournal::instance();
    JournalEntry entry;
    entry.transferId = QStringLiteral("journal-test-id");
    entry.type = TransferType::Download;
    entry.filePath = QStringLiteral("/tmp/example.bin");
    entry.partPath = QStringLiteral("/tmp/example.bin.part");
    entry.peerAddress = QStringLiteral("127.0.0.1");
    entry.state = TransferState::Transfering;
    entry.dataSize = 4096;
    entry.bytesTransferred = 1024;
    entry.fingerprint = TransferJournal::makeFingerprint(entry.transferId, entry.dataSize, entry.filePath);
    entry.updatedAtMs = QDateTime::currentMSecsSinceEpoch();

    QVERIFY(journal->upsert(entry));
    const QList<JournalEntry> loaded = journal->loadAll();
    bool found = false;
    for (const JournalEntry& item : loaded) {
        if (item.transferId == entry.transferId) {
            found = true;
            QCOMPARE(item.fingerprint, entry.fingerprint);
            QCOMPARE(item.bytesTransferred, entry.bytesTransferred);
            break;
        }
    }
    QVERIFY(found);
    QVERIFY(journal->remove(entry.transferId));
}

void TransferTest::batch10_journalFingerprintMismatchResetsPart()
{
    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString downloadDir = tempRoot.path() + "/downloads";
    QVERIFY(QDir().mkpath(downloadDir));

    Settings* settings = Settings::instance();
    settings->setDownloadDir(downloadDir);
    settings->setVerifyChecksum(false);
    settings->setResumePartialDownloads(true);
    settings->setReplaceExistingFile(true);
    settings->setTlsEnabled(false);
    settings->setJournalEnabled(true);

    const QString finalPath = downloadDir + "/mismatch.bin";
    const QString partPath = finalPath + ".part";
    QFile part(partPath);
    QVERIFY(part.open(QIODevice::WriteOnly));
    QCOMPARE(part.write(QByteArray(128, 'a')), 128);
    part.close();

    const QString transferId = QStringLiteral("fp-mismatch-id");
    JournalEntry entry;
    entry.transferId = transferId;
    entry.type = TransferType::Download;
    entry.filePath = finalPath;
    entry.partPath = partPath;
    entry.peerAddress = QStringLiteral("127.0.0.1");
    entry.state = TransferState::Transfering;
    entry.dataSize = 256;
    entry.bytesTransferred = 128;
    entry.fingerprint = QStringLiteral("deadbeef");
    entry.updatedAtMs = QDateTime::currentMSecsSinceEpoch();
    QVERIFY(TransferJournal::instance()->upsert(entry));

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    settings->setTransferPort(server.serverPort());

    Device localDevice;
    localDevice.setAddress(QHostAddress::LocalHost);
    localDevice.setName("test-peer");

    Receiver* receiver = nullptr;
    QObject::connect(&server, &QTcpServer::newConnection, [&]() {
        receiver = new Receiver(localDevice, server.nextPendingConnection());
    });

    QTcpSocket client;
    client.connectToHost(QHostAddress::LocalHost, settings->getTransferPort());
    QVERIFY(client.waitForConnected(3000));
    QTRY_VERIFY(receiver != nullptr);

    const QJsonObject header({
        {"name", "mismatch.bin"},
        {"folder", ""},
        {"size", 256},
        {"verify", false},
        {"transfer_id", transferId}
    });
    const QByteArray headerData = QJsonDocument(header).toJson(QJsonDocument::Compact);
    writePacket(client, headerData.size(), PacketType::Header, headerData);
    QVERIFY(client.waitForBytesWritten(3000));

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 3000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (receiver->getTransferInfo()->getState() == TransferState::Transfering)
            break;
    }

    QCOMPARE(receiver->getTransferInfo()->getState(), TransferState::Transfering);
    QCOMPARE(receiver->getBytesWritten(), 0);
    QVERIFY(!QFile::exists(partPath) || QFileInfo(partPath).size() == 0);

    TransferJournal::instance()->remove(transferId);
}

void TransferTest::batch10_admissionBusyOffsetAckFailsSender()
{
    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString sourcePath = tempRoot.path() + "/busy.bin";
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(QByteArray("busy")), qint64(4));
    source.close();

    Settings* settings = Settings::instance();
    settings->setTlsEnabled(false);
    settings->setVerifyChecksum(false);
    settings->setTransferOffsetAckTimeoutMs(60000);
    settings->setTransferRetryMax(0);

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    settings->setTransferPort(server.serverPort());

    Device localDevice;
    localDevice.setAddress(QHostAddress::LocalHost);
    localDevice.setName("test-peer");

    QObject::connect(&server, &QTcpServer::newConnection, [&]() {
        QTcpSocket* socket = server.nextPendingConnection();
        auto readPacket = [socket](PacketType& typeOut, QByteArray& payloadOut) -> bool {
            qint32 packetSize = -1;
            QElapsedTimer waitTimer;
            waitTimer.start();
            while (socket->bytesAvailable() < static_cast<int>(sizeof(qint32)) && waitTimer.elapsed() < 3000) {
                socket->waitForReadyRead(50);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            }
            if (socket->bytesAvailable() < static_cast<int>(sizeof(qint32)))
                return false;
            socket->read(reinterpret_cast<char*>(&packetSize), sizeof(packetSize));
            waitTimer.restart();
            while (socket->bytesAvailable() < packetSize + 1 && waitTimer.elapsed() < 3000) {
                socket->waitForReadyRead(50);
                QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
            }
            if (socket->bytesAvailable() < packetSize + 1)
                return false;
            typeOut = static_cast<PacketType>(socket->read(1).at(0));
            payloadOut = socket->read(packetSize);
            return true;
        };

        PacketType type = PacketType::Header;
        QByteArray payload;
        if (!readPacket(type, payload) || type != PacketType::Header)
            return;

        const QJsonObject busy({{QStringLiteral("busy"), true}, {QStringLiteral("retry_after_ms"), 1000}});
        const QByteArray busyPayload = QJsonDocument(busy).toJson(QJsonDocument::Compact);
        writePacket(*socket, busyPayload.size(), PacketType::OffsetAck, busyPayload);
        socket->flush();
    });

    Sender sender(localDevice, QString(), sourcePath);
    QVERIFY(sender.start());

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (sender.getTransferInfo()->getState() == TransferState::Failed)
            break;
    }

    QCOMPARE(sender.getTransferInfo()->getState(), TransferState::Failed);
    QCOMPARE(sender.getTransferInfo()->getFailureReason(), TransferFailureReason::AdmissionBusy);
}

void TransferTest::batch10_soakRepeatedTransfers()
{
    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString downloadDir = tempRoot.path() + "/soak-downloads";
    QVERIFY(QDir().mkpath(downloadDir));

    const QString sourcePath = tempRoot.path() + "/soak.bin";
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QByteArray payload(64 * 1024, 's');
    for (int i = 0; i < payload.size(); ++i)
        payload[i] = char(i & 0xff);
    QCOMPARE(source.write(payload), payload.size());
    source.close();

    Settings* settings = Settings::instance();
    settings->setJournalEnabled(true);

    int successes = 0;
    for (int i = 0; i < 5; ++i) {
        const QString iterDir = downloadDir + QStringLiteral("/iter-%1").arg(i);
        QVERIFY(QDir().mkpath(iterDir));
        const TransferState state = runTransfer(sourcePath, iterDir, true, false, 1);
        if (state == TransferState::Finish)
            ++successes;
    }

    QCOMPARE(successes, 5);
}

void TransferTest::batch10_uploadRetryAfterDisconnect()
{
    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString downloadDir = tempRoot.path() + "/downloads";
    QVERIFY(QDir().mkpath(downloadDir));

    const QString sourcePath = tempRoot.path() + "/retry.bin";
    const QByteArray payload(256 * 1024, 'r');
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(payload), payload.size());
    source.close();

    Settings* settings = Settings::instance();
    settings->setDownloadDir(downloadDir);
    settings->setVerifyChecksum(true);
    settings->setResumePartialDownloads(true);
    settings->setReplaceExistingFile(true);
    settings->setTlsEnabled(false);
    settings->setParallelStreams(1);
    settings->setTransferRetryMax(2);
    settings->setTransferRetryBaseMs(100);

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    settings->setTransferPort(server.serverPort());

    Device localDevice;
    localDevice.setAddress(QHostAddress::LocalHost);
    localDevice.setName("test-peer");

    int connectionCount = 0;
    bool forcedDisconnect = false;
    QObject::connect(&server, &QTcpServer::newConnection, [&]() {
        ++connectionCount;
        QTcpSocket* socket = server.nextPendingConnection();
        new Receiver(localDevice, socket);
    });

    Sender sender(localDevice, QString(), sourcePath);
    QObject::connect(sender.getTransferInfo(), &TransferInfo::statsChanged, &sender, [&]() {
        if (!forcedDisconnect && sender.getTransferInfo()->getAttempt() == 0
            && sender.getTransferInfo()->getState() == TransferState::Transfering
            && sender.getTransferInfo()->getBytesTransferred() >= 32 * 1024) {
            forcedDisconnect = true;
            if (sender.getSocket())
                sender.getSocket()->abort();
        }
    });
    QVERIFY(sender.start());

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 20000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (sender.getTransferInfo()->getState() == TransferState::Finish)
            break;
    }

    QVERIFY(forcedDisconnect);
    QVERIFY(connectionCount >= 2);
    QCOMPARE(sender.getTransferInfo()->getState(), TransferState::Finish);
    QVERIFY(sender.getTransferInfo()->getAttempt() >= 1);

    const QString expectedPath = downloadDir + "/retry.bin";
    QVERIFY(QFile::exists(expectedPath));
    QCOMPARE(Util::fileSha256(expectedPath), Util::fileSha256(sourcePath));
}

void TransferTest::interop_legacyMinimalOffsetAck()
{
    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString downloadDir = tempRoot.path() + "/downloads";
    QVERIFY(QDir().mkpath(downloadDir));

    const QString sourcePath = tempRoot.path() + "/legacy-ack.bin";
    const QByteArray payload(48 * 1024, 'l');
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(payload), payload.size());
    source.close();

    Settings* settings = Settings::instance();
    settings->setDownloadDir(downloadDir);
    settings->setVerifyChecksum(true);
    settings->setResumePartialDownloads(false);
    settings->setReplaceExistingFile(true);
    settings->setTlsEnabled(false);
    settings->setParallelStreams(1);

    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost, 0));
    settings->setTransferPort(server.serverPort());

    Device localDevice;
    localDevice.setAddress(QHostAddress::LocalHost);
    localDevice.setName("test-peer");

    QObject::connect(&server, &QTcpServer::newConnection, [&]() {
        QTcpSocket* socket = server.nextPendingConnection();
        QTimer::singleShot(0, [socket, downloadDir]() {
            auto readPacket = [socket](PacketType& typeOut, QByteArray& payloadOut) -> bool {
                qint32 packetSize = -1;
                QElapsedTimer waitTimer;
                waitTimer.start();
                while (socket->bytesAvailable() < static_cast<int>(sizeof(qint32)) && waitTimer.elapsed() < 5000) {
                    socket->waitForReadyRead(50);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                }
                if (socket->bytesAvailable() < static_cast<int>(sizeof(qint32)))
                    return false;
                socket->read(reinterpret_cast<char*>(&packetSize), sizeof(packetSize));
                waitTimer.restart();
                while (socket->bytesAvailable() < packetSize + 1 && waitTimer.elapsed() < 5000) {
                    socket->waitForReadyRead(50);
                    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
                }
                if (socket->bytesAvailable() < packetSize + 1)
                    return false;
                typeOut = static_cast<PacketType>(socket->read(1).at(0));
                payloadOut = socket->read(packetSize);
                return true;
            };

            PacketType type = PacketType::Header;
            QByteArray packetPayload;
            if (!readPacket(type, packetPayload) || type != PacketType::Header)
                return;

            const QJsonObject ack({{QStringLiteral("offset"), 0}});
            const QByteArray ackData = QJsonDocument(ack).toJson(QJsonDocument::Compact);
            writePacket(*socket, ackData.size(), PacketType::OffsetAck, ackData);
            socket->flush();

            QFile out(downloadDir + "/legacy-ack.bin");
            if (!out.open(QIODevice::WriteOnly))
                return;

            while (true) {
                if (!readPacket(type, packetPayload))
                    continue;
                if (type == PacketType::Data)
                    out.write(packetPayload);
                else if (type == PacketType::Finish)
                    break;
            }
            out.close();
        });
    });

    Sender sender(localDevice, QString(), sourcePath);
    QVERIFY(sender.start());

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 15000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (sender.getTransferInfo()->getState() == TransferState::Finish)
            break;
    }

    QCOMPARE(sender.getTransferInfo()->getState(), TransferState::Finish);
    const QString expectedPath = downloadDir + "/legacy-ack.bin";
    QVERIFY(QFile::exists(expectedPath));
    QCOMPARE(Util::fileSha256(expectedPath), Util::fileSha256(sourcePath));
}

void TransferTest::batch10_journalCrashRecoveryStartup()
{
    Settings* settings = Settings::instance();
    settings->setJournalEnabled(true);

    JournalEntry orphan;
    orphan.transferId = QStringLiteral("orphan-crash-id");
    orphan.type = TransferType::Download;
    orphan.filePath = QStringLiteral("/tmp/orphan.bin");
    orphan.partPath = QStringLiteral("/tmp/orphan.bin.part");
    orphan.peerAddress = QStringLiteral("127.0.0.1");
    orphan.state = TransferState::Transfering;
    orphan.dataSize = 8192;
    orphan.bytesTransferred = 4096;
    orphan.fingerprint = TransferJournal::makeFingerprint(orphan.transferId, orphan.dataSize, orphan.filePath);
    orphan.updatedAtMs = QDateTime::currentMSecsSinceEpoch();
    QVERIFY(TransferJournal::instance()->upsert(orphan));

    QString summary;
    const int retained = TransferJournal::instance()->recoverOnStartup(&summary);
    QCOMPARE(retained, 1);
    QVERIFY(summary.contains(QStringLiteral("1")));

    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());
    const QString downloadDir = tempRoot.path() + "/downloads";
    QVERIFY(QDir().mkpath(downloadDir));

    const QString sourcePath = tempRoot.path() + "/post-crash.bin";
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    const QByteArray payload(32 * 1024, 'c');
    QCOMPARE(source.write(payload), payload.size());
    source.close();

    const TransferState state = runTransfer(sourcePath, downloadDir, true, true, 1);
    QCOMPARE(state, TransferState::Finish);
    QCOMPARE(Util::fileSha256(downloadDir + "/post-crash.bin"), Util::fileSha256(sourcePath));

    TransferJournal::instance()->remove(orphan.transferId);
}

void TransferTest::batch10_journalChurnBurst()
{
    Settings* settings = Settings::instance();
    settings->setJournalEnabled(true);

    TransferJournal* journal = TransferJournal::instance();
    const int bursts = 50;
    for (int i = 0; i < bursts; ++i) {
        JournalEntry entry;
        entry.transferId = QStringLiteral("churn-%1").arg(i);
        entry.type = TransferType::Download;
        entry.filePath = QStringLiteral("/tmp/churn-%1.bin").arg(i);
        entry.partPath = entry.filePath + QStringLiteral(".part");
        entry.peerAddress = QStringLiteral("127.0.0.1");
        entry.state = TransferState::Transfering;
        entry.dataSize = 1024 * (i + 1);
        entry.bytesTransferred = i * 512;
        entry.fingerprint = TransferJournal::makeFingerprint(entry.transferId, entry.dataSize, entry.filePath);
        entry.updatedAtMs = QDateTime::currentMSecsSinceEpoch();
        QVERIFY(journal->upsert(entry));

        if (i >= 3)
            QVERIFY(journal->remove(QStringLiteral("churn-%1").arg(i - 3)));
    }

    const QList<JournalEntry> remaining = journal->loadAll();
    QCOMPARE(remaining.size(), 3);
    for (const JournalEntry& entry : remaining)
        journal->remove(entry.transferId);
}

void TransferTest::batch10_randomizedInterruptionCampaign()
{
    QRandomGenerator rng(0xC0FFEE);
    const int campaigns = 6;

    for (int campaign = 0; campaign < campaigns; ++campaign) {
        QTemporaryDir tempRoot;
        QVERIFY2(tempRoot.isValid(), qPrintable(QStringLiteral("campaign %1").arg(campaign)));

        const QString downloadDir = tempRoot.path() + "/downloads";
        QVERIFY(QDir().mkpath(downloadDir));

        const int payloadSize = (96 * 1024) + rng.bounded(32 * 1024);
        const int abortThreshold = (8 * 1024) + rng.bounded(48 * 1024);

    const QString sourcePath = tempRoot.path() + QStringLiteral("/rand.bin");
    const QByteArray payload(payloadSize, static_cast<char>('a' + (campaign % 26)));
        QFile source(sourcePath);
        QVERIFY(source.open(QIODevice::WriteOnly));
        QCOMPARE(source.write(payload), payload.size());
        source.close();

        Settings* settings = Settings::instance();
        settings->setDownloadDir(downloadDir);
        settings->setVerifyChecksum(true);
        settings->setResumePartialDownloads(true);
        settings->setReplaceExistingFile(true);
        settings->setTlsEnabled(false);
        settings->setParallelStreams(1);
        settings->setTransferRetryMax(3);
        settings->setTransferRetryBaseMs(50);
        settings->setJournalEnabled(true);

        QTcpServer server;
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        settings->setTransferPort(server.serverPort());

        Device localDevice;
        localDevice.setAddress(QHostAddress::LocalHost);
        localDevice.setName("test-peer");

        QObject::connect(&server, &QTcpServer::newConnection, [&]() {
            new Receiver(localDevice, server.nextPendingConnection());
        });

        Sender sender(localDevice, QString(), sourcePath);
        bool forcedDisconnect = false;
        QObject::connect(sender.getTransferInfo(), &TransferInfo::statsChanged, &sender, [&]() {
            if (!forcedDisconnect && sender.getTransferInfo()->getAttempt() == 0
                && sender.getTransferInfo()->getState() == TransferState::Transfering
                && sender.getTransferInfo()->getBytesTransferred() >= abortThreshold) {
                forcedDisconnect = true;
                if (sender.getSocket())
                    sender.getSocket()->abort();
            }
        });
        QVERIFY(sender.start());

        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 30000) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            if (sender.getTransferInfo()->getState() == TransferState::Finish)
                break;
        }

        const QString finalPath = downloadDir + QStringLiteral("/rand.bin");
        QVERIFY2(forcedDisconnect, qPrintable(QStringLiteral("campaign %1 did not interrupt").arg(campaign)));
        QVERIFY2(isFileAbsentOrChecksumValid(finalPath, sourcePath),
                  qPrintable(QStringLiteral("campaign %1 left corrupt final file").arg(campaign)));
        QCOMPARE(sender.getTransferInfo()->getState(), TransferState::Finish);
        QCOMPARE(Util::fileSha256(finalPath), Util::fileSha256(sourcePath));
    }
}

void TransferTest::batch10_chunkBoundaryResumeIntegrity()
{
    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString downloadDir = tempRoot.path() + "/downloads";
    QVERIFY(QDir().mkpath(downloadDir));

    const QString sourcePath = tempRoot.path() + "/boundary.bin";
    const int boundary = 73 * 1024 + 513;
    QByteArray payload(boundary + 16 * 1024, 'b');
    for (int i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<char>((i * 7) & 0xff);

    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(payload), payload.size());
    source.close();

    const QString partPath = downloadDir + "/boundary.bin.part";
    QFile part(partPath);
    QVERIFY(part.open(QIODevice::WriteOnly));
    QCOMPARE(part.write(payload.constData(), boundary), boundary);
    part.close();

    const TransferState state = runTransfer(sourcePath, downloadDir, true, true, 1);
    QCOMPARE(state, TransferState::Finish);

    const QString expectedPath = downloadDir + "/boundary.bin";
    QVERIFY(QFile::exists(expectedPath));
    QVERIFY(!QFile::exists(partPath));
    QCOMPARE(Util::fileSha256(expectedPath), Util::fileSha256(sourcePath));
}

void TransferTest::batch10_soakShipGateSlo()
{
    bool ok = false;
    int iterations = qEnvironmentVariableIntValue("LANSHARE_SOAK_ITERATIONS", &ok);
    if (!ok || iterations <= 0)
        iterations = 40;

    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString downloadDir = tempRoot.path() + "/slo-downloads";
    QVERIFY(QDir().mkpath(downloadDir));

    const QString sourcePath = tempRoot.path() + "/slo.bin";
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QByteArray payload(24 * 1024, 'o');
    for (int i = 0; i < payload.size(); ++i)
        payload[i] = static_cast<char>((i * 3) & 0xff);
    QCOMPARE(source.write(payload), payload.size());
    source.close();

    Settings* settings = Settings::instance();
    settings->setJournalEnabled(true);

    int successes = 0;
    for (int i = 0; i < iterations; ++i) {
        const QString iterDir = downloadDir + QStringLiteral("/iter-%1").arg(i);
        QVERIFY(QDir().mkpath(iterDir));
        const TransferState state = runTransfer(sourcePath, iterDir, true, false, 1 + (i % 2));
        if (state == TransferState::Finish)
            ++successes;
    }

    const int maxFailures = static_cast<int>(std::floor(iterations * 0.005));
    const int requiredSuccesses = iterations - maxFailures;
    QVERIFY2(successes >= requiredSuccesses,
             qPrintable(QStringLiteral("SLO miss: %1/%2 successes (need >= %3)")
                            .arg(successes)
                            .arg(iterations)
                            .arg(requiredSuccesses)));
}

void TransferTest::batch10_transferLogStatsParsing()
{
    const QString sample = QStringLiteral(
        "transfer transfer_id=a phase=start peer=127.0.0.1 code=- attempt=0 msg=x\n"
        "transfer transfer_id=a phase=finish peer=127.0.0.1 code=- attempt=0 msg=x\n"
        "transfer transfer_id=b phase=failed peer=127.0.0.1 code=timeout attempt=1 msg=x\n"
        "transfer transfer_id=c phase=retry peer=127.0.0.1 code=peer_disconnected attempt=2 msg=x\n"
        "transfer transfer_id=d phase=recovery peer=127.0.0.1 code=disconnected attempt=0 msg=x\n");

    const TransferLogStats stats = AppLog::parseTransferStats(sample);
    QCOMPARE(stats.starts, 1);
    QCOMPARE(stats.finishes, 1);
    QCOMPARE(stats.failures, 1);
    QCOMPARE(stats.retries, 1);
    QCOMPARE(stats.recoveries, 1);
    QCOMPARE(stats.failuresByCode.value(QStringLiteral("timeout")), 1);
    QVERIFY(AppLog::formatTransferStatsSummary(stats).contains(QStringLiteral("Failures: 1")));
}

void TransferTest::gap_transferServerAdmissionRetrySuccess()
{
    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString downloadDir = tempRoot.path() + "/downloads";
    QVERIFY(QDir().mkpath(downloadDir));

    const QString largePath = tempRoot.path() + "/large.bin";
    QFile largeFile(largePath);
    QVERIFY(largeFile.open(QIODevice::WriteOnly));
    QCOMPARE(largeFile.write(QByteArray(8 * 1024 * 1024, 'L')), 8 * 1024 * 1024);
    largeFile.close();

    const QString smallPath = tempRoot.path() + "/small.bin";
    const QByteArray smallPayload(128 * 1024, 's');
    QFile smallFile(smallPath);
    QVERIFY(smallFile.open(QIODevice::WriteOnly));
    QCOMPARE(smallFile.write(smallPayload), smallPayload.size());
    smallFile.close();

    Settings* settings = Settings::instance();
    settings->setDownloadDir(downloadDir);
    settings->setVerifyChecksum(true);
    settings->setResumePartialDownloads(false);
    settings->setReplaceExistingFile(true);
    settings->setTlsEnabled(false);
    settings->setMaxConcurrentDownloads(1);
    settings->setTransferRetryMax(3);
    settings->setTransferRetryBaseMs(50);

    DeviceBroadcaster broadcaster;
    DeviceListModel devList(&broadcaster);
    TransferServer transferServer(&devList);
    QVERIFY(startTransferServer(&transferServer, &devList));

    const Device peer = makeLocalPeerDevice();

    Sender senderLarge(peer, QString(), largePath);
    Sender senderSmall(peer, QString(), smallPath);
    QVERIFY(senderLarge.start());

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 10000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (senderLarge.getTransferInfo()->getState() == TransferState::Transfering
            && senderLarge.getTransferInfo()->getBytesTransferred() > 0) {
            break;
        }
    }
    QVERIFY(senderLarge.getTransferInfo()->getState() == TransferState::Transfering);

    QVERIFY(transferServer.activeDownloadCount() >= 1);
    QVERIFY(senderSmall.start());

    while (timer.elapsed() < 60000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        if (senderLarge.getTransferInfo()->getState() == TransferState::Finish
            && senderSmall.getTransferInfo()->getState() == TransferState::Finish) {
            break;
        }
    }

    QCOMPARE(senderLarge.getTransferInfo()->getState(), TransferState::Finish);
    QCOMPARE(senderSmall.getTransferInfo()->getState(), TransferState::Finish);
    QVERIFY2(senderSmall.getTransferInfo()->getAttempt() >= 1,
             "Expected admission-busy retry before small transfer completed");

    const QString smallOut = downloadDir + "/small.bin";
    QVERIFY(QFile::exists(smallOut));
    QCOMPARE(Util::fileSha256(smallOut), Util::fileSha256(smallPath));
}

void TransferTest::gap_tlsLoopbackTransfer()
{
    QTemporaryDir tlsDir;
    QVERIFY(tlsDir.isValid());
    TlsHelper::setTlsConfigDirForTests(tlsDir.path());

    QString tlsError;
    if (!TlsHelper::ensureServerCredentials(&tlsError)) {
        TlsHelper::clearTlsConfigDirForTests();
        QSKIP(qPrintable(QStringLiteral("openssl unavailable: %1").arg(tlsError)));
    }

    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString downloadDir = tempRoot.path() + "/downloads";
    QVERIFY(QDir().mkpath(downloadDir));

    const QString sourcePath = tempRoot.path() + "/tls.bin";
    const QByteArray payload(256 * 1024, 't');
    QFile source(sourcePath);
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(payload), payload.size());
    source.close();

    Settings* settings = Settings::instance();
    settings->setDownloadDir(downloadDir);
    settings->setVerifyChecksum(true);
    settings->setResumePartialDownloads(false);
    settings->setReplaceExistingFile(true);
    settings->setTlsEnabled(true);

    DeviceBroadcaster broadcaster;
    DeviceListModel devList(&broadcaster);
    TransferServer transferServer(&devList);
    QVERIFY(startTransferServer(&transferServer, &devList));

    const Device peer = makeLocalPeerDevice();
    Receiver* receiver = nullptr;
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setInterval(20000);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&transferServer, &TransferServer::newReceiverAdded, [&](Receiver* rec) {
        receiver = rec;
        QObject::connect(rec->getTransferInfo(), &TransferInfo::done, &loop, &QEventLoop::quit);
        QObject::connect(rec->getTransferInfo(), &TransferInfo::errorOcurred, &loop, &QEventLoop::quit);
    });

    Sender sender(peer, QString(), sourcePath);
    QVERIFY(sender.start());

    timeout.start();
    loop.exec();
    timeout.stop();

    TlsHelper::clearTlsConfigDirForTests();

    QVERIFY(receiver != nullptr);
    QCOMPARE(receiver->getTransferInfo()->getState(), TransferState::Finish);
    QCOMPARE(sender.getTransferInfo()->getState(), TransferState::Finish);

    const QString expectedPath = downloadDir + "/tls.bin";
    QVERIFY(QFile::exists(expectedPath));
    QCOMPARE(Util::fileSha256(expectedPath), Util::fileSha256(sourcePath));
}

void TransferTest::gap_concurrentMultiSender()
{
    QTemporaryDir tempRoot;
    QVERIFY(tempRoot.isValid());

    const QString downloadDir = tempRoot.path() + "/downloads";
    QVERIFY(QDir().mkpath(downloadDir));

    Settings* settings = Settings::instance();
    settings->setDownloadDir(downloadDir);
    settings->setVerifyChecksum(true);
    settings->setResumePartialDownloads(false);
    settings->setReplaceExistingFile(true);
    settings->setTlsEnabled(false);
    settings->setMaxConcurrentDownloads(0);
    settings->setParallelStreams(2);

    const int senderCount = 3;
    QVector<QString> sourcePaths;
    sourcePaths.reserve(senderCount);
    for (int i = 0; i < senderCount; ++i) {
        const QString path = tempRoot.path() + QStringLiteral("/burst-%1.bin").arg(i);
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        QByteArray payload(384 * 1024, static_cast<char>('a' + i));
        for (int b = 0; b < payload.size(); ++b)
            payload[b] = static_cast<char>((b + i * 17) & 0xff);
        QCOMPARE(file.write(payload), payload.size());
        file.close();
        sourcePaths.push_back(path);
    }

    DeviceBroadcaster broadcaster;
    DeviceListModel devList(&broadcaster);
    TransferServer transferServer(&devList);
    QVERIFY(startTransferServer(&transferServer, &devList));

    const Device peer = makeLocalPeerDevice();
    QVector<Sender*> senders;
    senders.reserve(senderCount);

    int finishedReceivers = 0;
    QObject::connect(&transferServer, &TransferServer::newReceiverAdded, [&](Receiver* rec) {
        QObject::connect(rec->getTransferInfo(), &TransferInfo::done, [&]() {
            ++finishedReceivers;
        });
        QObject::connect(rec->getTransferInfo(), &TransferInfo::errorOcurred, [&]() {
            ++finishedReceivers;
        });
    });

    for (int i = 0; i < senderCount; ++i) {
        auto* sender = new Sender(peer, QString(), sourcePaths.at(i));
        QVERIFY(sender->start());
        senders.push_back(sender);
    }

    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < 45000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        int sendersDone = 0;
        for (Sender* sender : senders) {
            if (sender->getTransferInfo()->getState() == TransferState::Finish)
                ++sendersDone;
        }
        if (sendersDone == senderCount && finishedReceivers >= senderCount)
            break;
    }

    for (int i = 0; i < senderCount; ++i) {
        QCOMPARE(senders.at(i)->getTransferInfo()->getState(), TransferState::Finish);
        const QString expectedPath = downloadDir + QStringLiteral("/burst-%1.bin").arg(i);
        QVERIFY(QFile::exists(expectedPath));
        QCOMPARE(Util::fileSha256(expectedPath), Util::fileSha256(sourcePaths.at(i)));
    }
    QCOMPARE(finishedReceivers, senderCount);

    qDeleteAll(senders);
}

QTEST_MAIN(TransferTest)
#include "transfer_test.moc"
