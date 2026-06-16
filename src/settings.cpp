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
#include <QStandardPaths>

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
    if (!dir.isEmpty() && QDir(dir).exists())
        mDownloadDir = dir;
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
    mDownloadDir = settings.value("DownloadDir", getDefaultDownloadPath()).toString();

    if (!QDir(mDownloadDir).exists()) {
        QDir dir;
        dir.mkpath(mDownloadDir);
    }

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
}

QString Settings::getDefaultDownloadPath()
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

