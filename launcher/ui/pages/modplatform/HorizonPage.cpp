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

#include "HorizonPage.h"

#include <QLabel>
#include <QPushButton>
#include <QSysInfo>
#include <QVBoxLayout>

#include "DesktopServices.h"
#include "minecraft/horizon/HorizonSupport.h"
#include "modplatform/horizon/HorizonInstanceCreationTask.h"
#include "ui/dialogs/NewInstanceDialog.h"

HorizonPage::HorizonPage(NewInstanceDialog* dialog, QWidget* parent) : QWidget(parent), dialog(dialog)
{
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);

    m_title = new QLabel(this);
    QFont titleFont = m_title->font();
    titleFont.setBold(true);
    titleFont.setPointSize(titleFont.pointSize() + 4);
    m_title->setFont(titleFont);
    layout->addWidget(m_title);

    m_text = new QLabel(this);
    m_text->setWordWrap(true);
    m_text->setTextFormat(Qt::RichText);
    layout->addWidget(m_text);

    m_unsupportedText = new QLabel(this);
    m_unsupportedText->setWordWrap(true);
    m_unsupportedText->setVisible(false);
    layout->addWidget(m_unsupportedText);

    auto buttons = new QHBoxLayout();
    m_websiteButton = new QPushButton(this);
    connect(m_websiteButton, &QPushButton::clicked, this, [] { DesktopServices::openUrl(QUrl("https://playhorizon.dev")); });
    buttons->addWidget(m_websiteButton);
    m_githubButton = new QPushButton(this);
    connect(m_githubButton, &QPushButton::clicked, this,
            [] { DesktopServices::openUrl(QUrl(QString("https://github.com/%1").arg(Horizon::Repo))); });
    buttons->addWidget(m_githubButton);
    buttons->addStretch();
    layout->addLayout(buttons);

    layout->addStretch();

    updateTexts();
    suggestCurrent();
}

void HorizonPage::openedImpl()
{
    suggestCurrent();
}

void HorizonPage::suggestCurrent()
{
    if (!Horizon::isPlatformSupported()) {
        m_unsupportedText->setVisible(true);
        dialog->setSuggestedPack();
        return;
    }
    dialog->setSuggestedPack(tr("Horizon"), new HorizonInstanceCreationTask());
}

void HorizonPage::retranslate()
{
    updateTexts();
}

void HorizonPage::updateTexts()
{
    m_title->setText(tr("Horizon \u2014 Minecraft, rewritten in Rust"));
    m_text->setText(
        tr("<p>Horizon is <b>Minecraft: Java Edition rewritten from scratch in Rust</b> by the Horizon Team. It opens your worlds, "
           "generates the same terrain and plays by the same rules, without Java.</p>"
           "<p>Horizon contains none of Mojang's code or assets: it builds what it needs from the files of <b>your own copy of "
           "Minecraft: Java Edition</b>. Nothing about your existing instances changes; Horizon lives in its own instance like any "
           "other.</p>"
           "<p>Clicking OK downloads the newest Horizon build (the Nightly, about 75 MB) from GitHub and creates an instance for "
           "it. The first time you press Play, the launcher downloads the Minecraft files the build needs and prepares them, which "
           "takes a few minutes; after that, starting is instant. Each instance keeps the build it was created with.</p>"
           "<p>To play on servers you need to be signed in with a Microsoft account that owns Minecraft: Java Edition, like for "
           "the normal game. Single-player also works offline.</p>"));
    m_unsupportedText->setText(tr("Horizon does not publish a build for this computer (%1, %2), so a Horizon instance cannot be "
                                  "created here.")
                                   .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture()));
    m_websiteButton->setText(tr("Visit playhorizon.dev"));
    m_githubButton->setText(tr("Horizon on GitHub"));
}
