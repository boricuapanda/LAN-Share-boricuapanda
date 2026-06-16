#ifndef TLSHELPER_H
#define TLSHELPER_H

#include <QString>
#include <QStringList>
#include <QSslCertificate>
#include <QSslKey>

namespace TlsHelper
{
QString tlsConfigDir();
QString certificatePath();
QString privateKeyPath();
bool ensureServerCredentials(QString* errorMessage);
bool loadServerCredentials(QSslCertificate* certificate, QSslKey* key, QString* errorMessage);
bool checkAndPinPeerCertificate(const QString& peerId,
                                const QSslCertificate& certificate,
                                QString* errorMessage);
int pinnedPeerCount();
void clearPinnedPeers();
QStringList pinnedPeerIds();
QString pinnedFingerprint(const QString& peerId);
void removePinnedPeer(const QString& peerId);
    void upsertPinnedPeer(const QString& peerId, const QString& fingerprint);

    void setTlsConfigDirForTests(const QString& dir);
    void clearTlsConfigDirForTests();
}

#endif // TLSHELPER_H
