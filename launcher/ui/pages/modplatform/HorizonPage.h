// SPDX-License-Identifier: GPL-3.0-only
/*
 *  Fjord Launcher - Minecraft Launcher
 *  Copyright (C) 2026 Fjord Launcher Contributors
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, version 3.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include <QWidget>

#include "ui/pages/BasePage.h"

class NewInstanceDialog;
class QLabel;
class QPushButton;

/**
 * "New Instance" page for Horizon, the Rust reimplementation of
 * Minecraft: Java Edition (https://github.com/horizon-rust/horizon).
 * Creates a HorizonInstanceCreationTask; all the downloading and setting
 * up happens there and in PrepareHorizonResources at first launch.
 */
class HorizonPage : public QWidget, public BasePage {
    Q_OBJECT
   public:
    explicit HorizonPage(NewInstanceDialog* dialog, QWidget* parent = nullptr);
    ~HorizonPage() override = default;

    QString displayName() const override { return tr("Horizon"); }
    QIcon icon() const override { return QIcon::fromTheme("horizon"); }
    QString id() const override { return "horizon"; }
    QString helpPage() const override { return "Horizon"; }
    void openedImpl() override;
    void retranslate() override;

   private:
    void suggestCurrent();
    void updateTexts();

    NewInstanceDialog* dialog = nullptr;
    QLabel* m_title = nullptr;
    QLabel* m_text = nullptr;
    QLabel* m_unsupportedText = nullptr;
    QPushButton* m_websiteButton = nullptr;
    QPushButton* m_githubButton = nullptr;
};
