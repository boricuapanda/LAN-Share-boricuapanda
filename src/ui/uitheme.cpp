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

// Brand: blue #1A6DAA, teal #14A3B8, white content
constexpr QColor kBluePrimary{0x1a, 0x6d, 0xaa};
constexpr QColor kTealAccent{0x14, 0xa3, 0xb8};
constexpr QColor kTextOnWhite{0x1b, 0x4f, 0x72};
constexpr QColor kTextMuted{0x5a, 0x8f, 0xad};

QString loadStyleSheet(const QString& resourcePath)
{
    QFile file(resourcePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();

    return QString::fromUtf8(file.readAll());
}

QPalette brandLightPalette()
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(255, 255, 255));
    palette.setColor(QPalette::WindowText, kTextOnWhite);
    palette.setColor(QPalette::Base, QColor(255, 255, 255));
    palette.setColor(QPalette::AlternateBase, QColor(244, 250, 252));
    palette.setColor(QPalette::ToolTipBase, QColor(255, 255, 255));
    palette.setColor(QPalette::ToolTipText, kTextOnWhite);
    palette.setColor(QPalette::Text, kTextOnWhite);
    palette.setColor(QPalette::Button, QColor(232, 244, 250));
    palette.setColor(QPalette::ButtonText, kTextOnWhite);
    palette.setColor(QPalette::BrightText, QColor(220, 53, 69));
    palette.setColor(QPalette::Link, kTealAccent);
    palette.setColor(QPalette::Highlight, kTealAccent);
    palette.setColor(QPalette::HighlightedText, QColor(255, 255, 255));
    palette.setColor(QPalette::PlaceholderText, kTextMuted);
    palette.setColor(QPalette::Mid, QColor(140, 180, 204));
    palette.setColor(QPalette::LinkVisited, QColor(192, 57, 43));
    return palette;
}

QPalette brandDarkPalette()
{
    QPalette palette;
    palette.setColor(QPalette::Window, QColor(15, 45, 66));
    palette.setColor(QPalette::WindowText, QColor(230, 240, 248));
    palette.setColor(QPalette::Base, QColor(20, 55, 80));
    palette.setColor(QPalette::AlternateBase, QColor(25, 65, 92));
    palette.setColor(QPalette::ToolTipBase, QColor(30, 70, 100));
    palette.setColor(QPalette::ToolTipText, QColor(255, 255, 255));
    palette.setColor(QPalette::Text, QColor(230, 240, 248));
    palette.setColor(QPalette::Button, QColor(26, 109, 170));
    palette.setColor(QPalette::ButtonText, QColor(255, 255, 255));
    palette.setColor(QPalette::BrightText, QColor(255, 100, 100));
    palette.setColor(QPalette::Link, QColor(45, 212, 191));
    palette.setColor(QPalette::Highlight, QColor(45, 212, 191));
    palette.setColor(QPalette::HighlightedText, QColor(15, 45, 66));
    palette.setColor(QPalette::PlaceholderText, QColor(140, 180, 200));
    palette.setColor(QPalette::Mid, QColor(100, 140, 170));
    palette.setColor(QPalette::LinkVisited, QColor(231, 76, 60));
    return palette;
}

QString darkStyleOverrides()
{
    return QStringLiteral(
        "QMainWindow { background-color: #0f2d42; }\n"
        "QToolBar { background-color: #154569; }\n"
        "QToolBar::separator { background-color: #2a6a9a; }\n"
        "QStatusBar { background-color: #0f2d42; color: #c5dff0; border-top: 1px solid #2a6a9a; }\n"
        "QTableView { background-color: #143750; alternate-background-color: #19425e; }\n"
        "QHeaderView::section { background-color: #19425e; color: #c5dff0; border-bottom: 2px solid #2dd4bf; }\n"
        "QLabel#sectionHeader { color: #5ec8e0; }\n"
        "QLabel#emptyStateLabel, QLabel#senderEmptyLabel, QLabel#receiverEmptyLabel { color: #7eb8d4; }\n"
        "QPushButton:flat { color: #c5dff0; }\n"
        "QPushButton:flat:hover:enabled { background-color: #1a4d6e; }\n"
        "QPushButton:flat:disabled { color: #4a7a96; }\n"
        "QProgressBar { background-color: #19425e; border-color: #2a6a9a; color: #c5dff0; }\n"
        "QProgressBar::chunk { background-color: #2dd4bf; }\n"
        "QSplitter::handle { background-color: #2a6a9a; }\n");
}

} // namespace

void UiTheme::apply(QApplication* app)
{
    if (!app)
        return;

    const UiThemeMode mode = Settings::instance()->getUiTheme();
    QString styleSheet = loadStyleSheet(QStringLiteral(":/style/app.qss"));

    app->setStyle(QStyleFactory::create(QStringLiteral("Fusion")));

    if (mode == UiThemeMode::Dark) {
        app->setPalette(brandDarkPalette());
        styleSheet += darkStyleOverrides();
    } else {
        // System and Light both use the blue/teal brand on white
        app->setPalette(brandLightPalette());
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
        return kTealAccent;
    case TransferState::Finish: {
        const bool darkBackground = palette.color(QPalette::Window).lightness() < 128;
        return darkBackground ? QColor(45, 212, 191) : QColor(13, 122, 110);
    }
    case TransferState::Failed:
    case TransferState::Cancelled:
    case TransferState::Disconnected:
        return palette.color(QPalette::BrightText);
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

QIcon UiTheme::appIcon(const QString& resourcePath)
{
    return QIcon(resourcePath);
}
