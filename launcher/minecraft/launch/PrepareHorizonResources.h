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

#include <QProcess>

#include "launch/LaunchStep.h"
#include "minecraft/horizon/HorizonSupport.h"
#include "net/NetJob.h"
#include "tasks/SequentialTask.h"

class MinecraftInstance;

/**
 * Makes sure the resources of a Horizon instance are prepared for the
 * installed build, following the Horizon Launch API
 * (https://github.com/horizon-rust/horizon/blob/main/docs/api/launch.md):
 *
 * 1. Ask the build what it needs (`horizon-prepare --launch-api`).
 * 2. Fast path: `horizon-prepare --check` says the resources are current.
 * 3. Otherwise, download the pinned Minecraft version's files with the
 *    regular machinery (client jar, libraries, assets), fetch the version
 *    JSON and the server jar from Mojang, and run
 *    `horizon-prepare --prepare --complete --progress json`, forwarding the
 *    machine-readable progress.
 *
 * Horizon ships none of Mojang's files: everything the preparer reads is
 * the player's own Minecraft, fetched the same way as for a Java instance.
 */
class PrepareHorizonResources : public LaunchStep {
    Q_OBJECT
   public:
    explicit PrepareHorizonResources(LaunchTask* parent);
    ~PrepareHorizonResources() override = default;

    void executeTask() override;
    bool canAbort() const override { return true; }
    bool abort() override;

   private:
    enum class Stage { Probe, Check, Prepare };

    void runPrepareTool(const QStringList& arguments, Stage stage);
    void onToolFinished(int exitCode, QProcess::ExitStatus status);
    void readPrepareOutput();

    void ensureGameFiles();
    void fetchMojangFiles();
    void fetchVersionJson();
    void downloadJars();
    void startPrepare();

    /// Folder to hand to horizon-prepare for the Java it runs, derived from
    /// the instance's Java. Empty when there is nothing sensible to pass.
    QString javaHome() const;

    MinecraftInstance* instance() const;

   private:
    QString m_buildDir;
    Horizon::LaunchApiInfo m_info;
    bool m_versionFixed = false;

    QProcess* m_process = nullptr;
    Stage m_stage = Stage::Probe;
    QByteArray m_prepareOutBuffer;
    QString m_prepareStdErr;
    QString m_prepareError;

    SequentialTask::Ptr m_filesTask;
    NetJob::Ptr m_netJob;
    QByteArray m_manifestData;
    QByteArray m_versionJsonData;
    QString m_versionJsonPath;
    QString m_clientJar;
    QString m_serverJar;
};
