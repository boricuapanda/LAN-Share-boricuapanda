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

#include <QNetworkInterface>
#include <QHostInfo>
#include <QUuid>
#include <QSettings>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>
#include <QStandardPaths>
#include <QStorageInfo>

#include "settings.h"

#define DefaultBroadcastPort        56780
#define DefaultTransferPort         17116
#define DefaultBroadcastInterval    5000    // 5 secs
#define DefaultFileBufferSize       (1024 * 1024)      // 1 MB
#define MaxFileBufferSize           (16 * 1024 * 1024) // 16 MB
#define DefaultTransferIdleTimeoutMs 120000
#define DefaultMaxPacketSize        (20 * 1024 * 1024)
#define DefaultMaxConcurrentDownloads 8
#define DefaultTransferRetryMax     2
#define DefaultTransferRetryBaseMs  1000
#define DefaultTransferOffsetAckTimeoutMs 30000
#define DefaultJournalRetentionDays 7
#define MinParallelStreams          1
#define MaxParallelStreams          8

namespace {

UiThemeMode uiThemeFromString(const QString& value)
{
    if (value == QLatin1String("light"))
        return UiThemeMode::Light;
    if (value == QLatin1String("dark"))
        return UiThemeMode::Dark;
    return UiThemeMode::System;
}

QString uiThemeToString(UiThemeMode mode)
{
    switch (mode) {
    case UiThemeMode::Light:
        return QStringLiteral("light");
    case UiThemeMode::Dark:
        return QStringLiteral("dark");
    case UiThemeMode::System:
    default:
        return QStringLiteral("system");
    }
}

void setDirectoryError(QString* errorMessage, const QString& message)
{
    if (errorMessage)
        *errorMessage = message;
}

bool ensureWritableDirectory(const QString& rawPath, QString* errorMessage)
{
    const QString path = QDir::cleanPath(rawPath.trimmed());
    if (path.isEmpty()) {
        setDirectoryError(errorMessage, QStringLiteral("Download folder is empty."));
        return false;
    }

#if defined(Q_OS_LINUX)
    auto mediaVolumeRootForPath = [](const QString& cleanPath) -> QString {
        const QString userName = QString::fromLocal8Bit(qgetenv("USER"));
        const QStringList roots = {
            QStringLiteral("/run/media/%1/").arg(userName),
            QStringLiteral("/media/%1/").arg(userName),
            QStringLiteral("/media/")
        };

        for (const QString& root : roots) {
            if (!cleanPath.startsWith(root))
                continue;
            const QString rest = cleanPath.mid(root.size());
            if (rest.isEmpty())
                return QString();
            const int slash = rest.indexOf(QDir::separator());
            return root + (slash >= 0 ? rest.left(slash) : rest);
        }
        return QString();
    };

    const QString mediaRoot = mediaVolumeRootForPath(path);
    if (!mediaRoot.isEmpty()) {
        const QFileInfo mediaRootInfo(mediaRoot);
        const QStorageInfo storage(mediaRoot);
        if (!mediaRootInfo.exists() || !storage.isValid() || !storage.isReady()
            || QDir::cleanPath(storage.rootPath()) != mediaRoot) {
            setDirectoryError(errorMessage,
                              QStringLiteral("Media volume is not mounted: %1").arg(mediaRoot));
            return false;
        }
    }
#endif

    QDir dir(path);
    if (!dir.exists() && !QDir().mkpath(path)) {
        setDirectoryError(errorMessage,
                          QStringLiteral("Download folder does not exist and could not be created: %1")
                              .arg(path));
        return false;
    }

    const QFileInfo info(path);
    if (!info.exists()) {
        setDirectoryError(errorMessage, QStringLiteral("Download folder does not exist: %1").arg(path));
        return false;
    }
    if (!info.isDir()) {
        setDirectoryError(errorMessage, QStringLiteral("Download path is not a folder: %1").arg(path));
        return false;
    }
    if (!info.isWritable()) {
        setDirectoryError(errorMessage, QStringLiteral("Download folder is not writable: %1").arg(path));
        return false;
    }

    const QString probePath = QDir(path).filePath(
        QStringLiteral(".lanshare-write-test-%1")
            .arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
    QFile probe(probePath);
    if (!probe.open(QIODevice::WriteOnly)) {
        setDirectoryError(errorMessage,
                          QStringLiteral("Download folder failed write test: %1 (%2)")
                              .arg(path, probe.errorString()));
        return false;
    }
    if (probe.write("ok") != 2) {
        setDirectoryError(errorMessage,
                          QStringLiteral("Download folder failed write test: %1 (%2)")
                              .arg(path, probe.errorString()));
        probe.close();
        probe.remove();
        return false;
    }
    probe.close();
    if (probe.error() != QFileDevice::NoError) {
        setDirectoryError(errorMessage,
                          QStringLiteral("Download folder failed write test: %1 (%2)")
                              .arg(path, probe.errorString()));
        probe.remove();
        return false;
    }
    probe.remove();

    setDirectoryError(errorMessage, QString());
    return true;
}

} // namespace

Settings* Settings::obj = new Settings;
Settings::Settings()
{
    mThisDevice = Device();
    mThisDevice.setId(QUuid::createUuid().toString());
    mThisDevice.setAddress(QHostAddress::LocalHost);
    mThisDevice.setOSName(OS_NAME);

    foreach (QHostAddress address, QNetworkInterface::allAddresses()) {
        if (address.protocol() == QAbstractSocket::IPv4Protocol &&
                address != QHostAddress::LocalHost) {
            mThisDevice.setAddress(address);
            break;
        }
    }

    loadSettings();
}

void Settings::setDeviceName(const QString &name)
{
    mThisDevice.setName(name);
}

void Settings::setBroadcastPort(quint16 port)
{
    if (port > 0)
        mBCPort = port;
}

void Settings::setBroadcastInterval(quint16 interval)
{
    mBCInterval = interval;
}

void Settings::setTransferPort(quint16 port)
{
    if (port > 0)
        mTransferPort = port;
}

void Settings::setFileBufferSize(qint32 size)
{
    if (size > 0 && size <= MaxFileBufferSize)
        mFileBuffSize = size;
}

void Settings::setDownloadDir(const QString& dir)
{
    const QString path = QDir::cleanPath(dir.trimmed());
    QString errorMessage;
    if (!path.isEmpty() && ensureWritableDirectory(path, &errorMessage)) {
        mDownloadDir = path;
    } else if (!path.isEmpty()) {
        qWarning().noquote() << QStringLiteral("Ignoring invalid LAN Share download folder: %1 (%2)")
                                    .arg(path, errorMessage);
    }
}

void Settings::setReplaceExistingFile(bool replace)
{
    mReplaceExistingFile = replace;
}

void Settings::setVerifyChecksum(bool verify)
{
    mVerifyChecksum = verify;
}

void Settings::setResumePartialDownloads(bool resume)
{
    mResumePartialDownloads = resume;
}

void Settings::setMaxConcurrentTransfers(int count)
{
    if (count >= 0 && count <= 100)
        mMaxConcurrentTransfers = count;
}

void Settings::setParallelStreams(int count)
{
    if (count < MinParallelStreams)
        mParallelStreams = MinParallelStreams;
    else if (count > MaxParallelStreams)
        mParallelStreams = MaxParallelStreams;
    else
        mParallelStreams = count;
}

void Settings::setAuthEnabled(bool enabled)
{
    mAuthEnabled = enabled;
}

void Settings::setAuthToken(const QString& token)
{
    mAuthToken = token;
}

void Settings::setTlsEnabled(bool enabled)
{
    mTlsEnabled = enabled;
}

void Settings::setTransferIdleTimeoutMs(int ms)
{
    if (ms >= 0 && ms <= 3600000)
        mTransferIdleTimeoutMs = ms;
}

void Settings::setMaxPacketSize(qint32 size)
{
    if (size >= 1024 && size <= 64 * 1024 * 1024)
        mMaxPacketSize = size;
}

void Settings::setMaxConcurrentDownloads(int count)
{
    if (count >= 0 && count <= 100)
        mMaxConcurrentDownloads = count;
}

void Settings::setTransferRetryMax(int count)
{
    if (count >= 0 && count <= 10)
        mTransferRetryMax = count;
}

void Settings::setTransferRetryBaseMs(int ms)
{
    if (ms >= 100 && ms <= 60000)
        mTransferRetryBaseMs = ms;
}

void Settings::setTransferOffsetAckTimeoutMs(int ms)
{
    if (ms >= 1000 && ms <= 300000)
        mTransferOffsetAckTimeoutMs = ms;
}

void Settings::setJournalEnabled(bool enabled)
{
    mJournalEnabled = enabled;
}

void Settings::setJournalRetentionDays(int days)
{
    if (days >= 0 && days <= 365)
        mJournalRetentionDays = days;
}

void Settings::setUiTheme(UiThemeMode mode)
{
    mUiTheme = mode;
}

void Settings::loadSettings()
{
    QSettings settings(SETTINGS_FILE);
    mThisDevice.setName(settings.value("DeviceName", QHostInfo::localHostName()).toString());
    mBCPort = settings.value("BroadcastPort", DefaultBroadcastPort).value<quint16>();
    mTransferPort = settings.value("TransferPort", DefaultTransferPort).value<quint16>();
    mFileBuffSize = settings.value("FileBufferSize", DefaultFileBufferSize).value<quint32>();
    mDownloadDir = QDir::cleanPath(settings.value("DownloadDir", getDefaultDownloadPath()).toString());

    mBCInterval = settings.value("BroadcastInterval", DefaultBroadcastInterval).value<quint16>();
    mReplaceExistingFile = settings.value("ReplaceExistingFile", false).toBool();
    mVerifyChecksum = settings.value("VerifyChecksum", true).toBool();
    mResumePartialDownloads = settings.value("ResumePartialDownloads", true).toBool();
    mMaxConcurrentTransfers = settings.value("MaxConcurrentTransfers", 4).toInt();
    setParallelStreams(settings.value("ParallelStreams", 1).toInt());
    mAuthEnabled = settings.value("AuthEnabled", false).toBool();
    mAuthToken = settings.value("AuthToken", QString()).toString();
    mTlsEnabled = settings.value("TlsEnabled", true).toBool();
    mTransferIdleTimeoutMs = settings.value("TransferIdleTimeoutMs", DefaultTransferIdleTimeoutMs).toInt();
    mMaxPacketSize = settings.value("MaxPacketSize", DefaultMaxPacketSize).value<qint32>();
    mMaxConcurrentDownloads = settings.value("MaxConcurrentDownloads", DefaultMaxConcurrentDownloads).toInt();
    mTransferRetryMax = settings.value("TransferRetryMax", DefaultTransferRetryMax).toInt();
    mTransferRetryBaseMs = settings.value("TransferRetryBaseMs", DefaultTransferRetryBaseMs).toInt();
    mTransferOffsetAckTimeoutMs = settings.value("TransferOffsetAckTimeoutMs", DefaultTransferOffsetAckTimeoutMs).toInt();
    mJournalEnabled = settings.value("JournalEnabled", true).toBool();
    mJournalRetentionDays = settings.value("JournalRetentionDays", DefaultJournalRetentionDays).toInt();
    mUiTheme = uiThemeFromString(settings.value("UiTheme", QStringLiteral("system")).toString());

    QString downloadDirError;
    if (!ensureDownloadDirReady(&downloadDirError)) {
        const QString configuredDir = mDownloadDir;
        mDownloadDir = getDefaultDownloadPath();

        QString fallbackError;
        if (ensureDownloadDirReady(&fallbackError)) {
            settings.setValue("DownloadDir", mDownloadDir);
            settings.sync();
            qWarning().noquote()
                << QStringLiteral("LAN Share download folder unavailable: %1 (%2). Reset to %3.")
                       .arg(configuredDir, downloadDirError, mDownloadDir);
        } else {
            qWarning().noquote()
                << QStringLiteral("LAN Share download folder unavailable: %1 (%2). Default folder also failed: %3.")
                       .arg(configuredDir, downloadDirError, fallbackError);
        }
    }
}

QString Settings::getDefaultDownloadPath() const
{
#if defined (Q_OS_WIN)
    return
        QStandardPaths::locate(QStandardPaths::DownloadLocation, QString(), QStandardPaths::LocateDirectory) + "LANShareDownloads";
#else
    return QDir::homePath() + QDir::separator() + "LANShareDownloads";
#endif
}

void Settings::saveSettings()
{
    QSettings settings(SETTINGS_FILE);
    settings.setValue("DeviceName", mThisDevice.getName());
    settings.setValue("BroadcastPort", mBCPort);
    settings.setValue("TransferPort", mTransferPort);
    settings.setValue("FileBufferSize", mFileBuffSize);
    settings.setValue("DownloadDir", mDownloadDir);
    settings.setValue("BroadcastInterval", mBCInterval);
    settings.setValue("ReplaceExistingFile", mReplaceExistingFile);
    settings.setValue("VerifyChecksum", mVerifyChecksum);
    settings.setValue("ResumePartialDownloads", mResumePartialDownloads);
    settings.setValue("MaxConcurrentTransfers", mMaxConcurrentTransfers);
    settings.setValue("ParallelStreams", mParallelStreams);
    settings.setValue("AuthEnabled", mAuthEnabled);
    settings.setValue("AuthToken", mAuthToken);
    settings.setValue("TlsEnabled", mTlsEnabled);
    settings.setValue("TransferIdleTimeoutMs", mTransferIdleTimeoutMs);
    settings.setValue("MaxPacketSize", mMaxPacketSize);
    settings.setValue("MaxConcurrentDownloads", mMaxConcurrentDownloads);
    settings.setValue("TransferRetryMax", mTransferRetryMax);
    settings.setValue("TransferRetryBaseMs", mTransferRetryBaseMs);
    settings.setValue("TransferOffsetAckTimeoutMs", mTransferOffsetAckTimeoutMs);
    settings.setValue("JournalEnabled", mJournalEnabled);
    settings.setValue("JournalRetentionDays", mJournalRetentionDays);
    settings.setValue("UiTheme", uiThemeToString(mUiTheme));
}

void Settings::reset()
{
    mThisDevice.setName(QHostInfo::localHostName());
    mBCPort = DefaultBroadcastPort;
    mTransferPort = DefaultTransferPort;
    mBCInterval = DefaultBroadcastInterval;
    mFileBuffSize = DefaultFileBufferSize;
    mDownloadDir = getDefaultDownloadPath();
    mVerifyChecksum = true;
    mResumePartialDownloads = true;
    mMaxConcurrentTransfers = 4;
    mParallelStreams = 1;
    mAuthEnabled = false;
    mAuthToken.clear();
    mTlsEnabled = true;
    mTransferIdleTimeoutMs = DefaultTransferIdleTimeoutMs;
    mMaxPacketSize = DefaultMaxPacketSize;
    mMaxConcurrentDownloads = DefaultMaxConcurrentDownloads;
    mTransferRetryMax = DefaultTransferRetryMax;
    mTransferRetryBaseMs = DefaultTransferRetryBaseMs;
    mTransferOffsetAckTimeoutMs = DefaultTransferOffsetAckTimeoutMs;
    mJournalEnabled = true;
    mJournalRetentionDays = DefaultJournalRetentionDays;
    mUiTheme = UiThemeMode::System;
}

quint16 Settings::getBroadcastPort() const
{
    return mBCPort; 
}

quint16 Settings::getTransferPort() const
{
    return mTransferPort;
}

quint16 Settings::getBroadcastInterval() const
{
    return mBCInterval;
}

qint32 Settings::getFileBufferSize() const 
{ 
    return mFileBuffSize; 
}

QString Settings::getDownloadDir() const
{
    return mDownloadDir;
}

bool Settings::ensureDownloadDirReady(QString* errorMessage) const
{
    return ensureWritableDirectory(mDownloadDir, errorMessage);
}

Device Settings::getMyDevice() const
{
    return mThisDevice;
}

QString Settings::getDeviceId() const
{
    return mThisDevice.getId();
}

QString Settings::getDeviceName() const
{
    return mThisDevice.getName();
}

QHostAddress Settings::getDeviceAddress() const
{
    return mThisDevice.getAddress();
}

bool Settings::getReplaceExistingFile() const
{
    return mReplaceExistingFile;
}

bool Settings::getVerifyChecksum() const
{
    return mVerifyChecksum;
}

bool Settings::getResumePartialDownloads() const
{
    return mResumePartialDownloads;
}

int Settings::getMaxConcurrentTransfers() const
{
    return mMaxConcurrentTransfers;
}

int Settings::getParallelStreams() const
{
    return mParallelStreams;
}

bool Settings::getAuthEnabled() const
{
    return mAuthEnabled;
}

QString Settings::getAuthToken() const
{
    return mAuthToken;
}

bool Settings::getTlsEnabled() const
{
    return mTlsEnabled;
}

int Settings::getTransferIdleTimeoutMs() const
{
    return mTransferIdleTimeoutMs;
}

qint32 Settings::getMaxPacketSize() const
{
    return mMaxPacketSize;
}

int Settings::getMaxConcurrentDownloads() const
{
    return mMaxConcurrentDownloads;
}

int Settings::getTransferRetryMax() const
{
    return mTransferRetryMax;
}

int Settings::getTransferRetryBaseMs() const
{
    return mTransferRetryBaseMs;
}

int Settings::getTransferOffsetAckTimeoutMs() const
{
    return mTransferOffsetAckTimeoutMs;
}

bool Settings::getJournalEnabled() const
{
    return mJournalEnabled;
}

int Settings::getJournalRetentionDays() const
{
    return mJournalRetentionDays;
}

UiThemeMode Settings::getUiTheme() const
{
    return mUiTheme;
}
