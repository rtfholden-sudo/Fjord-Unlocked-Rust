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

#include <LoggedProcess.h>
#include <launch/LaunchStep.h>
#include <minecraft/auth/AuthSession.h>

#include "MinecraftTarget.h"

class QProcess;

/**
 * Starts the native Horizon client of a Horizon instance and babysits it,
 * following the Horizon Launch API
 * (https://github.com/horizon-rust/horizon/blob/main/docs/api/launch.md):
 *
 * - the build is asked what it accepts first (`horizon-client --launch-api`),
 *   and only the options it lists are passed (anything else is refused with
 *   exit code 2);
 * - the signed-in account is handed over as the session JSON on standard
 *   input (`--session-stdin`), never on the command line, where other
 *   programs could read the token;
 * - exit codes are mapped the way the Launch API describes them.
 */
class HorizonPartLaunch : public LaunchStep {
    Q_OBJECT
   public:
    explicit HorizonPartLaunch(LaunchTask* parent);
    ~HorizonPartLaunch() override = default;

    void executeTask() override;
    bool abort() override;
    void proceed() override;
    bool canAbort() const override { return true; }
    void setWorkingDirectory(const QString& wd);
    void setAuthSession(AuthSessionPtr session) { m_session = std::move(session); }
    void setTargetToJoin(MinecraftTarget::Ptr targetToJoin) { m_targetToJoin = std::move(targetToJoin); }

   private slots:
    void on_state(LoggedProcess::State state);

   private:
    void probeFinished(int exitCode, const QByteArray& output);
    void startGame(const QStringList& acceptedOptions);

    LoggedProcess m_process;
    AuthSessionPtr m_session;
    MinecraftTarget::Ptr m_targetToJoin;
    QProcess* m_probe = nullptr;
    QByteArray m_sessionJson;
    bool m_sessionPending = false;
    bool m_proceeded = false;
};
