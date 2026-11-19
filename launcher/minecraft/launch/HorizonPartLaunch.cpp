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

#include "HorizonPartLaunch.h"

#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

#include "Application.h"
#include "Commandline.h"
#include "launch/LaunchTask.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/horizon/HorizonSupport.h"

HorizonPartLaunch::HorizonPartLaunch(LaunchTask* parent) : LaunchStep(parent), m_process(QStringConverter::Utf8)
{
    connect(&m_process, &LoggedProcess::log, this, &HorizonPartLaunch::logLines);
    connect(&m_process, &LoggedProcess::stateChanged, this, &HorizonPartLaunch::on_state);
}

void HorizonPartLaunch::executeTask()
{
    auto instance = m_parent->instance();
    const QString client = Horizon::clientExecutable(Horizon::buildDir(instance));
    if (!QFileInfo::exists(client)) {
        const char* reason =
            QT_TR_NOOP("This instance's Horizon build is missing. Delete the instance and create it again to reinstall Horizon.");
        emit logLine(tr(reason), MessageLevel::Fatal);
        emitFailed(tr(reason));
        return;
    }

    // Ask the build which options it takes before handing it any; the
    // Launch API refuses unknown options with exit code 2.
    m_probe = new QProcess(this);
    connect(m_probe, &QProcess::finished, this, [this](int exitCode) {
        const QByteArray output = m_probe->readAllStandardOutput();
        probeFinished(exitCode, output);
    });
    connect(m_probe, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            probeFinished(-1, {});
    });
    m_probe->start(client, { "--launch-api" });
}

void HorizonPartLaunch::probeFinished(int exitCode, const QByteArray& output)
{
    if (m_probe) {
        m_probe->deleteLater();
        m_probe = nullptr;
    }

    // The launch was aborted while the build was being asked; do not start
    // the game behind the user's back.
    if (!isRunning())
        return;

    QStringList accepted;
    if (exitCode == 0) {
        if (const auto info = Horizon::parseLaunchApi(output))
            accepted = info->options;
    }
    if (accepted.isEmpty()) {
        // The probe failed; fall back to the options the Launch API has had
        // since 0.1.0. If the build is broken, starting it will say so.
        accepted = { "--resources", "--game-dir", "--home", "--session-stdin" };
    }
    startGame(accepted);
}

void HorizonPartLaunch::startGame(const QStringList& acceptedOptions)
{
    auto instance = m_parent->instance();
    const QString buildDir = Horizon::buildDir(instance);
    const QString client = Horizon::clientExecutable(buildDir);
    auto accepts = [&acceptedOptions](const char* option) { return acceptedOptions.contains(QLatin1String(option)); };

    QStringList args;
    if (accepts("--resources"))
        args << "--resources" << Horizon::preparedDir(buildDir);
    if (accepts("--game-dir"))
        args << "--game-dir" << instance->gameRoot();
    if (accepts("--home"))
        args << "--home" << instance->gameRoot();

    if (accepts("--fullscreen") && instance->settings()->get("LaunchMaximized").toBool()) {
        args << "--fullscreen";
    } else {
        if (accepts("--width")) {
            const int width = instance->settings()->get("MinecraftWinWidth").toInt();
            if (width > 0)
                args << "--width" << QString::number(width);
        }
        if (accepts("--height")) {
            const int height = instance->settings()->get("MinecraftWinHeight").toInt();
            if (height > 0)
                args << "--height" << QString::number(height);
        }
    }

    if (m_targetToJoin) {
        if (!m_targetToJoin->world.isEmpty() && accepts("--quickPlaySingleplayer")) {
            args << "--quickPlaySingleplayer" << m_targetToJoin->world;
        } else if (!m_targetToJoin->address.isEmpty() && accepts("--quickPlayMultiplayer")) {
            QString address = m_targetToJoin->address;
            if (m_targetToJoin->port != 25565)
                address += QString(":%1").arg(m_targetToJoin->port);
            args << "--quickPlayMultiplayer" << address;
        }
    }

    m_sessionPending = false;
    if (m_session && accepts("--session-stdin")) {
        // The account is handed over on standard input, never on the command
        // line. Without a readable session the game still starts; online-mode
        // servers will simply refuse the player.
        QJsonObject session;
        session.insert("name", m_session->player_name);
        session.insert("uuid", m_session->uuid);
        session.insert("access_token", m_session->access_token);
        session.insert("user_type", "msa");
        m_sessionJson = QJsonDocument(session).toJson(QJsonDocument::Compact);
        args << "--session-stdin";
        m_sessionPending = true;
    }

    emit logLine(tr("Horizon arguments:\n  %1\n").arg(m_parent->censorPrivateInfo(args.join(" "))), MessageLevel::Launcher);

    m_process.setProcessEnvironment(instance->createLaunchEnvironment());

    // make detachable - this will keep the process running even if the object is destroyed
    m_process.setDetachable(true);

    QString wrapperCommandStr = instance->getWrapperCommand().trimmed();
    if (!wrapperCommandStr.isEmpty()) {
        wrapperCommandStr = m_parent->substituteVariables(wrapperCommandStr);
        auto wrapperArgs = Commandline::splitArgs(wrapperCommandStr);
        auto wrapperCommand = wrapperArgs.takeFirst();
        auto realWrapperCommand = QStandardPaths::findExecutable(wrapperCommand);
        if (realWrapperCommand.isEmpty()) {
            const char* reason = QT_TR_NOOP("The wrapper command \"%1\" couldn't be found.");
            emit logLine(tr(reason).arg(wrapperCommand), MessageLevel::Fatal);
            emitFailed(tr(reason).arg(wrapperCommand));
            return;
        }
        emit logLine(tr("Wrapper command is:\n%1\n").arg(wrapperCommandStr), MessageLevel::Launcher);
        m_process.start(realWrapperCommand, wrapperArgs + QStringList{ client } + args);
    } else {
        m_process.start(client, args);
    }
}

void HorizonPartLaunch::on_state(LoggedProcess::State state)
{
    switch (state) {
        case LoggedProcess::FailedToStart: {
            const char* reason = QT_TR_NOOP("Could not start Horizon: %1");
            emit logLine(tr(reason).arg(m_process.errorString()), MessageLevel::Fatal);
            emitFailed(tr(reason).arg(m_process.errorString()));
            return;
        }
        case LoggedProcess::Aborted:
        case LoggedProcess::Crashed: {
            m_parent->setPid(-1);
            m_parent->instance()->setMinecraftRunning(false);
            emitFailed(tr("Game crashed."));
            return;
        }
        case LoggedProcess::Finished: {
            auto instance = m_parent->instance();
            if (instance->settings()->get("CloseAfterLaunch").toBool())
                APPLICATION->showMainWindow();

            m_parent->setPid(-1);
            instance->setMinecraftRunning(false);

            const int exitCode = m_process.exitCode();
            if (exitCode == 0) {
                emitSucceeded();
                return;
            }
            if (exitCode == 2) {
                const char* reason = QT_TR_NOOP(
                    "Horizon refused the options it was started with (exit code 2). The refused option is named "
                    "in the log above; please report this as a bug in the launcher.");
                emit logLine(tr(reason), MessageLevel::Fatal);
                emitFailed(tr(reason));
                return;
            }
            // 1 and 101 (and anything unexpected): the game stopped on an
            // error, and on a crash the last log line names the report.
            emitFailed(tr("Horizon stopped on an error (exit code %1). If it crashed, the last line of the log names the saved "
                          "crash report.")
                           .arg(exitCode));
            return;
        }
        case LoggedProcess::Running: {
            emit logLine(tr("Horizon process ID: %1\n").arg(m_process.processId()), MessageLevel::Launcher);
            m_parent->setPid(m_process.processId());
            if (m_sessionPending) {
                m_process.write(m_sessionJson);
                m_process.closeWriteChannel();
                m_sessionPending = false;
            }
            emit readyForLaunch();
            return;
        }
        default:
            return;
    }
}

void HorizonPartLaunch::setWorkingDirectory(const QString& wd)
{
    m_process.setWorkingDirectory(wd);
}

void HorizonPartLaunch::proceed()
{
    if (!m_proceeded) {
        m_proceeded = true;
        m_parent->instance()->setMinecraftRunning(true);
    }
}

bool HorizonPartLaunch::abort()
{
    if (m_probe) {
        m_probe->kill();
        m_probe->deleteLater();
        m_probe = nullptr;
    }
    const auto state = m_process.state();
    if (state == LoggedProcess::Running || state == LoggedProcess::Starting)
        m_process.kill();
    return true;
}
