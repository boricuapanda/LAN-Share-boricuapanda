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

#ifndef SETTINGS_H
#define SETTINGS_H

#include <QHostAddress>
#include <QString>

#include "model/device.h"


#if defined (Q_OS_WIN)
    #define OS_NAME "Windows"
#elif defined (Q_OS_OSX)
    #define OS_NAME "Mac OSX"
#elif defined (Q_OS_LINUX)
    #define OS_NAME "Linux"
#else
    #define OS_NAME "Unknown"
#endif

const QString PROGRAM_NAME{"LAN Share"};
const QString PROGRAM_DESC{"A simple program that let you transfer files over local area network (LAN) easily."};
constexpr int PROGRAM_X_VER{1};
constexpr int PROGRAM_Y_VER{2};
constexpr int PROGRAM_Z_VER{4};
const QString SETTINGS_FILE{"LANSConfig"};

enum class UiThemeMode { System, Light, Dark };

class Settings
{
public:
    static Settings* instance() { return obj; }

    quint16 getBroadcastPort() const;
    quint16 getTransferPort() const;
    quint16 getBroadcastInterval() const;
    qint32 getFileBufferSize() const;
    QString getDownloadDir() const;

    Device getMyDevice() const;
    QString getDeviceId() const;
    QString getDeviceName() const;
    QHostAddress getDeviceAddress() const;
    bool getReplaceExistingFile() const;
    bool getVerifyChecksum() const;
    bool getResumePartialDownloads() const;
    int getMaxConcurrentTransfers() const;
    int getParallelStreams() const;
    bool getAuthEnabled() const;
    QString getAuthToken() const;
    bool getTlsEnabled() const;
    int getTransferIdleTimeoutMs() const;
    qint32 getMaxPacketSize() const;
    int getMaxConcurrentDownloads() const;
    int getTransferRetryMax() const;
    int getTransferRetryBaseMs() const;
    int getTransferOffsetAckTimeoutMs() const;
    bool getJournalEnabled() const;
    int getJournalRetentionDays() const;
    UiThemeMode getUiTheme() const;

    void setDeviceName(const QString& name);
    void setBroadcastPort(quint16 port);
    void setTransferPort(quint16 port);
    void setBroadcastInterval(quint16 interval);
    void setFileBufferSize(qint32 size);
    void setDownloadDir(const QString& dir);
    void setReplaceExistingFile(bool replace);
    void setVerifyChecksum(bool verify);
    void setResumePartialDownloads(bool resume);
    void setMaxConcurrentTransfers(int count);
    void setParallelStreams(int count);
    void setAuthEnabled(bool enabled);
    void setAuthToken(const QString& token);
    void setTlsEnabled(bool enabled);
    void setTransferIdleTimeoutMs(int ms);
    void setMaxPacketSize(qint32 size);
    void setMaxConcurrentDownloads(int count);
    void setTransferRetryMax(int count);
    void setTransferRetryBaseMs(int ms);
    void setTransferOffsetAckTimeoutMs(int ms);
    void setJournalEnabled(bool enabled);
    void setJournalRetentionDays(int days);
    void setUiTheme(UiThemeMode mode);

    void saveSettings();
    void reset();

private:
    Settings();
    void loadSettings();

    QString getDefaultDownloadPath();

    Device mThisDevice;
    quint16 mBCPort{0};
    quint16 mTransferPort{0};
    quint16 mBCInterval{0};
    qint32 mFileBuffSize{0};
    QString mDownloadDir;
    bool mReplaceExistingFile{false};
    bool mVerifyChecksum{true};
    bool mResumePartialDownloads{true};
    int mMaxConcurrentTransfers{4};
    int mParallelStreams{1};
    bool mAuthEnabled{false};
    QString mAuthToken;
    bool mTlsEnabled{true};
    int mTransferIdleTimeoutMs{120000};
    qint32 mMaxPacketSize{20 * 1024 * 1024};
    int mMaxConcurrentDownloads{8};
    int mTransferRetryMax{2};
    int mTransferRetryBaseMs{1000};
    int mTransferOffsetAckTimeoutMs{30000};
    bool mJournalEnabled{true};
    int mJournalRetentionDays{7};
    UiThemeMode mUiTheme{UiThemeMode::System};

    static Settings* obj;
};

#endif // SETTINGS_H
