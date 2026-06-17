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

#include <QApplication>
#include <QSslSocket>
#include <QtDebug>

#include "settings.h"
#include "ui/mainwindow.h"
#include "ui/uitheme.h"
#include "singleinstance.h"
#include "util.h"
#include "log.h"
#include "transfer/transferjournal.h"

int main(int argc, char *argv[])
{
    if (qEnvironmentVariableIsSet("WAYLAND_DISPLAY"))
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);

    QApplication app(argc, argv);
    UiTheme::apply(&app);
    app.setQuitOnLastWindowClosed(false);
    AppLog::install();
    AppLog::write(QStringLiteral("tls"),
                  QStringLiteral("Qt SSL support=%1 build='%2' runtime='%3'")
                      .arg(QSslSocket::supportsSsl() ? QStringLiteral("yes") : QStringLiteral("no"),
                           QSslSocket::sslLibraryBuildVersionString(),
                           QSslSocket::sslLibraryVersionString()));

    QString recoverySummary;
    TransferJournal::instance()->recoverOnStartup(&recoverySummary);
    if (!recoverySummary.isEmpty())
        AppLog::write(QStringLiteral("journal"), recoverySummary);

    SingleInstance si(PROGRAM_NAME);
    if (si.hasPreviousInstance()) {
        return EXIT_SUCCESS;
    }

    if (!si.start()) {
        qDebug() << si.getLastErrorString();
        return EXIT_FAILURE;
    }

    app.setApplicationDisplayName(PROGRAM_NAME);
    app.setApplicationName(PROGRAM_NAME);
    app.setApplicationVersion(Util::parseAppVersion());

    MainWindow mainWindow;
    if (!recoverySummary.isEmpty())
        mainWindow.showStartupMessage(recoverySummary);
    mainWindow.show();

    QObject::connect(&si, &SingleInstance::newInstanceCreated, [&mainWindow]() {
        mainWindow.setMainWindowVisibility(true);
    });

    return app.exec();
}
