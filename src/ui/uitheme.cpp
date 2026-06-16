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

#include "ui/uitheme.h"

#include <QFile>
#include <QStyle>
#include <QStyleFactory>

#include "settings.h"

namespace {

QString loadStyleSheet(const QString& resourcePath)
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    return QString::fromUtf8(file.readAll());
}

QPalette lightPalette()
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(240, 240, 240));
    palette.setColor(QPalette::WindowText, QColor(0, 0, 0));
    palette.setColor(QPalette::Base, QColor(255, 255, 255));
    palette.setColor(QPalette::AlternateBase, QColor(245, 245, 245));
    palette.setColor(QPalette::ToolTipBase, QColor(255, 255, 220));
    palette.setColor(QPalette::ToolTipText, QColor(0, 0, 0));
    palette.setColor(QPalette::Text, QColor(0, 0, 0));
    palette.setColor(QPalette::Button, QColor(240, 240, 240));
    palette.setColor(QPalette::ButtonText, QColor(0, 0, 0));
    palette.setColor(QPalette::BrightText, QColor(255, 0, 0));
    palette.setColor(QPalette::Link, QColor(42, 130, 218));
    palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    palette.setColor(QPalette::PlaceholderText, QColor(120, 120, 120));
    palette.setColor(QPalette::Mid, QColor(160, 160, 160));
    palette.setColor(QPalette::LinkVisited, QColor(192, 57, 43));
    return palette;
}

QPalette darkPalette()
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(53, 53, 53));
    palette.setColor(QPalette::WindowText, QColor(255, 255, 255));
    palette.setColor(QPalette::Base, QColor(35, 35, 35));
    palette.setColor(QPalette::AlternateBase, QColor(53, 53, 53));
    palette.setColor(QPalette::ToolTipBase, QColor(255, 255, 220));
    palette.setColor(QPalette::ToolTipText, QColor(0, 0, 0));
    palette.setColor(QPalette::Text, QColor(255, 255, 255));
    palette.setColor(QPalette::Button, QColor(53, 53, 53));
    palette.setColor(QPalette::ButtonText, QColor(255, 255, 255));
    palette.setColor(QPalette::BrightText, QColor(255, 0, 0));
    palette.setColor(QPalette::Link, QColor(42, 130, 218));
    palette.setColor(QPalette::Highlight, QColor(42, 130, 218));
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    palette.setColor(QPalette::PlaceholderText, QColor(127, 127, 127));
    palette.setColor(QPalette::Mid, QColor(127, 127, 127));
    palette.setColor(QPalette::LinkVisited, QColor(231, 76, 60));
    return palette;
}

QString modeStyleOverrides(UiThemeMode mode)
{
    if (mode == UiThemeMode::Dark) {
        return QStringLiteral(
            "QMainWindow { background-color: palette(window); }\n"
            "QTableView { background-color: palette(base); }\n");
    }

    if (mode == UiThemeMode::Light) {
        return QStringLiteral(
            "QMainWindow { background-color: palette(window); }\n"
            "QTableView { background-color: palette(base); }\n");
    }

    return QString();
}

} // namespace

void UiTheme::apply(QApplication* app)
{
    if (!app)
        return;

    static QString platformStyleName;
    if (platformStyleName.isEmpty())
        platformStyleName = app->style()->objectName();

    const UiThemeMode mode = Settings::instance()->getUiTheme();
    QString styleSheet = loadStyleSheet(QStringLiteral(":/style/app.qss"));

    if (mode == UiThemeMode::System) {
        if (QStyle* platformStyle = QStyleFactory::create(platformStyleName))
            app->setStyle(platformStyle);
        app->setPalette(app->style()->standardPalette());
    } else {
        app->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));
        app->setPalette(mode == UiThemeMode::Dark ? darkPalette() : lightPalette());
        styleSheet += modeStyleOverrides(mode);
    }

    app->setStyleSheet(styleSheet);
}

QColor UiTheme::stateColor(TransferState state, const QPalette& palette)
{
    switch (state) {
    case TransferState::Idle:
        return palette.color(QPalette::Text);
    case TransferState::Queued:
        return palette.color(QPalette::PlaceholderText);
    case TransferState::Waiting:
    case TransferState::Paused:
        return palette.color(QPalette::Mid);
    case TransferState::Transfering:
        return palette.color(QPalette::Link);
    case TransferState::Finish: {
        const bool darkBackground = palette.color(QPalette::Window).lightness() < 128;
        return darkBackground ? QColor(102, 187, 106) : QColor(46, 125, 50);
    }
    case TransferState::Failed:
    case TransferState::Cancelled:
    case TransferState::Disconnected: {
        QColor errorTone = palette.color(QPalette::LinkVisited);
        if (!errorTone.isValid())
            errorTone = QColor(192, 57, 43);
        return errorTone;
    }
    }

    return QColor();
}

QIcon UiTheme::themedIcon(const QString& freedesktopName, const QString& resourcePath)
{
    const QIcon themed = QIcon::fromTheme(freedesktopName);
    if (!themed.isNull())
        return themed;

    return QIcon(resourcePath);
}
