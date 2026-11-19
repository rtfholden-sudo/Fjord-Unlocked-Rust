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

#include <QTemporaryDir>

#include "InstanceTask.h"
#include "archive/ExtractZipTask.h"
#include "minecraft/horizon/HorizonSupport.h"
#include "net/NetJob.h"

class QProcess;

/**
 * Creates an instance that runs Horizon, the Rust reimplementation of
 * Minecraft: Java Edition (https://github.com/horizon-rust/horizon).
 *
 * The task downloads a Horizon build from the project's GitHub releases,
 * verifies it against the release's SHA256SUMS, unpacks it into the
 * instance's "horizon" folder and asks the build which Minecraft version it
 * needs (via `horizon-prepare --launch-api`, see Horizon's Launch API). The
 * created instance pins that Minecraft version as a regular component, so
 * the usual download machinery provides the client jar, libraries and
 * assets. The Mojang files are turned into resources Horizon can use at
 * first launch by PrepareHorizonResources, not here: creating the instance
 * stays a plain download and keeps working for games bought later.
 */
class HorizonInstanceCreationTask : public InstanceTask {
    Q_OBJECT
   public:
    explicit HorizonInstanceCreationTask(QString tag = QString(Horizon::NightlyTag));
    ~HorizonInstanceCreationTask() override = default;

    bool canAbort() const override { return true; }
    bool abort() override;

   protected:
    void executeTask() override;

   private slots:
    void releaseInfoDownloaded();
    void buildDownloaded();
    void downloadFailed(const QString& reason);
    void extractionFinished();
    void probeFinished(int exitCode);

   private:
    bool createInstanceFromBuild(const Horizon::LaunchApiInfo& info);
    [[nodiscard]] QString horizonDir() const;
    [[nodiscard]] QString prepareExecutable() const;

   private:
    QString m_tag;
    QString m_archiveName;
    QString m_releaseName;
    QUrl m_archiveUrl;
    QUrl m_sumsUrl;
    QByteArray m_releaseInfoData;

    QTemporaryDir m_tempDir;
    NetJob::Ptr m_netJob;
    Task::Ptr m_extractTask;
    QProcess* m_probe = nullptr;
};
