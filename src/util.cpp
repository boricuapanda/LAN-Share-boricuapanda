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

#include <QAbstractItemView>
#include <QCryptographicHash>
#include <QFileDialog>
#include <QFileInfo>
#include <QListView>
#include <QSet>
#include <QStorageInfo>
#include <QTreeView>
#include <QUrl>
#include <QWidget>

#include "util.h"
#include "settings.h"

namespace {

QList<QUrl> storageSidebarUrls()
{
    QSet<QString> paths;
    auto addPath = [&](const QString& path) {
        const QString cleaned = QDir::cleanPath(path);
        if (!cleaned.isEmpty() && QDir(cleaned).exists())
            paths.insert(cleaned);
    };

    addPath(QDir::homePath());
    addPath("/");
    addPath("/srv/temp-storage");
    addPath("/mnt");

    const QDir mediaDir("/run/media");
    if (mediaDir.exists()) {
        for (const QString& user : mediaDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
            const QDir userDir(mediaDir.filePath(user));
            for (const QString& mount : userDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
                addPath(userDir.filePath(mount));
        }
    }

    const QDir legacyMediaDir("/media");
    if (legacyMediaDir.exists()) {
        for (const QString& entry : legacyMediaDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot))
            addPath(legacyMediaDir.filePath(entry));
    }

    QList<QUrl> urls;
    for (const QString& path : paths)
        urls << QUrl::fromLocalFile(path);
    return urls;
}

void configureStorageDialog(QFileDialog& dialog, bool allowMultiple);

void configureDirectoryDialog(QFileDialog& dialog, bool allowMultiple)
{
    dialog.setFileMode(QFileDialog::Directory);
    dialog.setOption(QFileDialog::ShowDirsOnly, true);
    configureStorageDialog(dialog, allowMultiple);
}

void configureOpenFileDialog(QFileDialog& dialog)
{
    dialog.setFileMode(QFileDialog::ExistingFiles);
    configureStorageDialog(dialog, true);
}

void configureStorageDialog(QFileDialog& dialog, bool allowMultiple)
{
    dialog.setOption(QFileDialog::DontUseNativeDialog, true);
    dialog.setObjectName(QStringLiteral("storageFileDialog"));
    dialog.setSidebarUrls(storageSidebarUrls());
    dialog.setStyleSheet(dialog.styleSheet() + QStringLiteral(
        "QFileDialog#storageFileDialog QComboBox::drop-down {"
        "  subcontrol-origin: border;"
        "  subcontrol-position: top right;"
        "  width: 34px;"
        "  border-left: 1px solid #d3dbe5;"
        "  background-color: #f1f4f8;"
        "}"
        "QFileDialog#storageFileDialog QComboBox::down-arrow {"
        "  image: url(:/img/chevron-down.xpm);"
        "  width: 12px;"
        "  height: 8px;"
        "}"
        "QFileDialog#storageFileDialog QComboBox::drop-down:hover {"
        "  background-color: #e3ebf2;"
        "}"));

    const auto updateTitle = [&dialog](const QString& path) {
        const QString freeSpace = Util::freeSpaceString(path);
        const QString baseTitle = dialog.windowTitle().section(QStringLiteral(" - "), 0, 0);
        dialog.setWindowTitle(freeSpace.isEmpty() ? baseTitle : baseTitle + QStringLiteral(" - ") + freeSpace);
    };
    QObject::connect(&dialog, &QFileDialog::directoryEntered, &dialog, updateTitle);
    QObject::connect(&dialog, &QFileDialog::currentChanged, &dialog, [&](const QString& path) {
        if (!path.isEmpty())
            updateTitle(QFileInfo(path).absolutePath());
    });
    updateTitle(dialog.directory().absolutePath());

    if (allowMultiple) {
        if (QListView* listView = dialog.findChild<QListView*>("listView"))
            listView->setSelectionMode(QAbstractItemView::ExtendedSelection);
        if (QTreeView* treeView = dialog.findChild<QTreeView*>("treeView"))
            treeView->setSelectionMode(QAbstractItemView::ExtendedSelection);
    }
}

} // namespace

QString Util::sizeToString(qint64 size)
{
    int count = 0;
    double f_size = size;
    while (f_size >= 1024) {
        f_size /= 1024;
        count++;
    }

    QString suffix;
    switch (count) {
    case 0 : suffix = " B"; break;
    case 1 : suffix = " KB"; break;
    case 2 : suffix = " MB"; break;
    case 3 : suffix = " GB"; break;
    case 4 : suffix = " TB"; break;
    }

    return QString::number(f_size, 'f', 2).append(suffix);
}

QVector< QPair<QString, QString> >
    Util::getInnerDirNameAndFullFilePath(const QDir& startingDir, const QString& innerDirName)
{
    QVector< QPair<QString, QString> > pairs;

    QFileInfoList fiList = startingDir.entryInfoList(QDir::NoDotAndDotDot | QDir::Files);
    for (const auto& fi : fiList)
        pairs.push_back( QPair<QString, QString>(innerDirName, fi.filePath()) );

    fiList = startingDir.entryInfoList(QDir::NoDotAndDotDot | QDir::Dirs);
    for (const auto& fi : fiList) {
        QString newInnerDirName;
        if (innerDirName.isEmpty())
            newInnerDirName = fi.fileName();
        else
            newInnerDirName = innerDirName + QDir::separator() + fi.fileName();

        QVector< QPair<QString, QString> > otherPairs =
                getInnerDirNameAndFullFilePath( QDir(fi.filePath()), newInnerDirName );

        pairs.append(otherPairs);
    }

    return pairs;
}

QString Util::parseAppVersion(bool onlyVerNum)
{
    if (onlyVerNum) {
        return QString::number(PROGRAM_X_VER) + "." +
               QString::number(PROGRAM_Y_VER) + "." +
               QString::number(PROGRAM_Z_VER);
    }

    return "v " + QString::number(PROGRAM_X_VER) + "." +
           QString::number(PROGRAM_Y_VER) + "." +
           QString::number(PROGRAM_Z_VER) +
           " (" + QString(OS_NAME) + ")";
}

/*
 * cek file path (folderName+fileName).
 * jika file dengan nama "fileName" sudah ada
 * maka cek lagi untuk "fileName (1)" jika masih ada chek lagi untuk "fileName (2)" dst.
 * kemudian return file path untuk nama file yang belum ada.
 */
QString Util::getUniqueFileName(const QString& fileName, const QString& folderPath)
{
    int count = 1;
    QString originalFilePath = folderPath + QDir::separator() + fileName;
    QString fPath = originalFilePath;
    while (QFile::exists(fPath)) {
        QFileInfo fInfo(originalFilePath);
        QString baseName = fInfo.baseName() + " (" + QString::number(count) + ")";
        fPath = folderPath + QDir::separator() + baseName + "." + fInfo.completeSuffix();
        count++;
    }

    return fPath;
}

QString Util::selectExistingDirectory(QWidget* parent,
                                      const QString& title,
                                      const QString& startDir)
{
    const QString initialDir = startDir.isEmpty() ? QDir::homePath() : startDir;
    QFileDialog dialog(parent, title, initialDir);
    configureDirectoryDialog(dialog, false);
    if (dialog.exec() != QDialog::Accepted)
        return QString();

    const QStringList selected = dialog.selectedFiles();
    return selected.isEmpty() ? QString() : selected.first();
}

QStringList Util::selectExistingDirectories(QWidget* parent,
                                              const QString& title,
                                              const QString& startDir)
{
    const QString initialDir = startDir.isEmpty() ? QDir::homePath() : startDir;
    QFileDialog dialog(parent, title, initialDir);
    configureDirectoryDialog(dialog, true);
    if (dialog.exec() != QDialog::Accepted)
        return QStringList();

    return dialog.selectedFiles();
}

QStringList Util::selectOpenFileNames(QWidget* parent,
                                      const QString& title,
                                      const QString& startDir)
{
    const QString initialDir = startDir.isEmpty() ? QDir::homePath() : startDir;
    QFileDialog dialog(parent, title, initialDir);
    configureOpenFileDialog(dialog);
    if (dialog.exec() != QDialog::Accepted)
        return QStringList();

    return dialog.selectedFiles();
}

qint64 Util::availableBytes(const QString& path)
{
    const QStorageInfo storage(path);
    if (!storage.isValid() || !storage.isReady())
        return -1;
    return storage.bytesAvailable();
}

QString Util::fileSha256(const QString& filePath)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return QString();

    QCryptographicHash hash(QCryptographicHash::Sha256);
    const qint64 bufferSize = 1024 * 1024;
    QByteArray buffer;
    buffer.resize(bufferSize);

    while (true) {
        const qint64 bytesRead = file.read(buffer.data(), bufferSize);
        if (bytesRead <= 0)
            break;
        hash.addData(buffer.constData(), bytesRead);
    }

    return QString::fromLatin1(hash.result().toHex());
}

QString Util::freeSpaceString(const QString& path)
{
    const QStorageInfo storage(path);
    if (!storage.isValid() || !storage.isReady())
        return QString();

    return QObject::tr("Free: %1").arg(sizeToString(storage.bytesAvailable()));
}

QString Util::formatSpeed(double bytesPerSecond)
{
    if (bytesPerSecond < 1.0)
        return QStringLiteral("-");

    return QObject::tr("%1/s").arg(sizeToString((qint64)bytesPerSecond));
}

QString Util::formatEta(qint64 secondsRemaining)
{
    if (secondsRemaining < 0)
        return QObject::tr("ETA -");
    if (secondsRemaining == 0)
        return QObject::tr("ETA <1s");

    const qint64 hours = secondsRemaining / 3600;
    const qint64 minutes = (secondsRemaining % 3600) / 60;
    const qint64 seconds = secondsRemaining % 60;

    if (hours > 0)
        return QObject::tr("ETA %1h %2m").arg(hours).arg(minutes);
    if (minutes > 0)
        return QObject::tr("ETA %1m %2s").arg(minutes).arg(seconds);
    return QObject::tr("ETA %1s").arg(seconds);
}
