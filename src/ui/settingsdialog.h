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

#ifndef SETTINGSDIALOG_H
#define SETTINGSDIALOG_H

#include <QWidget>

class QLabel;
class QButtonGroup;
class QVBoxLayout;

namespace Ui {
class SettingsDialog;
}

class SettingsDialog : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog() override;

    void reloadFromSettings();

Q_SIGNALS:
    void cancelled();
    void saved();

private Q_SLOTS:
    void onCancelClicked();
    void onSaveClicked();
    void onResetClicked();
    void onSelectDownDirClicked();
    void onViewLogClicked();
    void onManageTlsTrustClicked();
    void onGenerateTokenClicked();
    void onParallelStreamsChanged(int value);

private:
    void assign();
    void setupStitchUi();
    void reorganizeSettingsTabs();
    void styleSettingsCards();
    void addNetworkPortHints();
    void addNetworkInfoBanner();
    void setupParallelStreamsBadge();
    void setupAuthTokenGenerate();
    void setupThemeSelector();
    void syncThemeButtons();

    Ui::SettingsDialog *ui;
    QLabel* mParallelStreamsBadge{nullptr};
    QVBoxLayout* mReliabilityLayout{nullptr};
    QButtonGroup* mThemeButtonGroup{nullptr};
};

#endif // SETTINGSDIALOG_H
