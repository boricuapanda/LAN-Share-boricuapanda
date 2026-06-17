#include <QDir>
#include <QFile>
#include <QObject>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>

#include "tlshelper.h"

namespace {

QString configBaseDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation) + QDir::separator() + "LANShare";
    return base;
}

bool generateSelfSignedCert(QString* errorMessage)
{
    QProcess process;
    QStringList args;
    args << "req"
         << "-x509"
         << "-nodes"
         << "-newkey"
         << "rsa:2048"
         << "-keyout" << TlsHelper::privateKeyPath()
         << "-out" << TlsHelper::certificatePath()
         << "-days" << "3650"
         << "-subj" << "/CN=LANShare Local Transfer";
    process.start("openssl", args);
    if (!process.waitForStarted()) {
        if (errorMessage) {
            *errorMessage = QObject::tr("TLS setup failed: openssl is not available in PATH.");
        }
        return false;
    }

    process.waitForFinished();
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
        if (errorMessage) {
            const QString output = QString::fromUtf8(process.readAllStandardError()).trimmed();
            *errorMessage = QObject::tr("TLS setup failed while generating certificate: %1")
                                .arg(output.isEmpty() ? QObject::tr("openssl exited with error.") : output);
        }
        return false;
    }

    return true;
}

QString& testTlsConfigDir()
{
    // Leak this intentionally so TLS path lookups remain safe during shutdown.
    static QString* dir = new QString;
    return *dir;
}

} // namespace

QString TlsHelper::tlsConfigDir()
{
    const QString testDir = testTlsConfigDir();
    if (!testDir.isEmpty())
        return testDir;
    return configBaseDir() + QDir::separator() + "tls";
}

void TlsHelper::setTlsConfigDirForTests(const QString& dir)
{
    testTlsConfigDir() = dir;
}

void TlsHelper::clearTlsConfigDirForTests()
{
    testTlsConfigDir().clear();
}

QString TlsHelper::certificatePath()
{
    return tlsConfigDir() + QDir::separator() + "transfer-cert.pem";
}

QString TlsHelper::privateKeyPath()
{
    return tlsConfigDir() + QDir::separator() + "transfer-key.pem";
}

bool TlsHelper::ensureServerCredentials(QString* errorMessage)
{
    QDir().mkpath(tlsConfigDir());
    const bool certExists = QFile::exists(certificatePath());
    const bool keyExists = QFile::exists(privateKeyPath());
    if (certExists && keyExists)
        return true;

    return generateSelfSignedCert(errorMessage);
}

bool TlsHelper::loadServerCredentials(QSslCertificate* certificate, QSslKey* key, QString* errorMessage)
{
    if (!certificate || !key) {
        if (errorMessage)
            *errorMessage = QObject::tr("TLS setup failed: invalid certificate output pointers.");
        return false;
    }

    if (!ensureServerCredentials(errorMessage))
        return false;

    QFile certFile(certificatePath());
    if (!certFile.open(QIODevice::ReadOnly)) {
        if (errorMessage)
            *errorMessage = QObject::tr("TLS setup failed: cannot read certificate file %1.").arg(certificatePath());
        return false;
    }

    QFile keyFile(privateKeyPath());
    if (!keyFile.open(QIODevice::ReadOnly)) {
        if (errorMessage)
            *errorMessage = QObject::tr("TLS setup failed: cannot read private key file %1.").arg(privateKeyPath());
        return false;
    }

    const QSslCertificate loadedCert(&certFile, QSsl::Pem);
    const QSslKey loadedKey(&keyFile, QSsl::Rsa, QSsl::Pem);
    if (loadedCert.isNull() || loadedKey.isNull()) {
        if (errorMessage)
            *errorMessage = QObject::tr("TLS setup failed: certificate or key is invalid.");
        return false;
    }

    *certificate = loadedCert;
    *key = loadedKey;
    return true;
}

bool TlsHelper::checkAndPinPeerCertificate(const QString& peerId,
                                           const QSslCertificate& certificate,
                                           QString* errorMessage)
{
    if (peerId.isEmpty() || certificate.isNull()) {
        if (errorMessage)
            *errorMessage = QObject::tr("TLS validation failed: peer identity or certificate is missing.");
        return false;
    }

    const QString fingerprint = QString::fromLatin1(certificate.digest(QCryptographicHash::Sha256).toHex());
    QSettings settings("LANSConfig");
    const QString key = QString("TlsPinned/%1").arg(peerId);
    const QString pinned = settings.value(key).toString();

    // TOFU: pin first seen cert and require exact fingerprint match afterwards.
    if (pinned.isEmpty()) {
        settings.setValue(key, fingerprint);
        return true;
    }

    if (pinned == fingerprint)
        return true;

    if (errorMessage) {
        *errorMessage = QObject::tr("TLS fingerprint mismatch for %1. Expected %2, got %3.")
                            .arg(peerId, pinned.left(16), fingerprint.left(16));
    }
    return false;
}

int TlsHelper::pinnedPeerCount()
{
    QSettings settings("LANSConfig");
    settings.beginGroup("TlsPinned");
    const int count = settings.allKeys().size();
    settings.endGroup();
    return count;
}

void TlsHelper::clearPinnedPeers()
{
    QSettings settings("LANSConfig");
    settings.beginGroup("TlsPinned");
    settings.remove("");
    settings.endGroup();
}

QStringList TlsHelper::pinnedPeerIds()
{
    QSettings settings("LANSConfig");
    settings.beginGroup("TlsPinned");
    const QStringList keys = settings.allKeys();
    settings.endGroup();
    return keys;
}

QString TlsHelper::pinnedFingerprint(const QString& peerId)
{
    if (peerId.isEmpty())
        return QString();

    QSettings settings("LANSConfig");
    return settings.value(QString("TlsPinned/%1").arg(peerId)).toString();
}

void TlsHelper::removePinnedPeer(const QString& peerId)
{
    if (peerId.isEmpty())
        return;

    QSettings settings("LANSConfig");
    settings.remove(QString("TlsPinned/%1").arg(peerId));
}

void TlsHelper::upsertPinnedPeer(const QString& peerId, const QString& fingerprint)
{
    if (peerId.isEmpty() || fingerprint.isEmpty())
        return;

    QSettings settings("LANSConfig");
    settings.setValue(QString("TlsPinned/%1").arg(peerId), fingerprint);
}
