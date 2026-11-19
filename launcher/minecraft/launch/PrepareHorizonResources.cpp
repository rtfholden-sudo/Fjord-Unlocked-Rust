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

#include "PrepareHorizonResources.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include "Application.h"
#include "FileSystem.h"
#include "launch/LaunchTask.h"
#include "minecraft/LaunchProfile.h"
#include "minecraft/Library.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/MinecraftLoadAndCheck.h"
#include "minecraft/PackProfile.h"
#include "net/ChecksumValidator.h"
#include "net/Download.h"

namespace {

bool fileMatchesChecksum(const QString& path, QCryptographicHash::Algorithm algorithm, const QString& expectedHex)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    QCryptographicHash hash(algorithm);
    if (!hash.addData(&file))
        return false;
    return QString::fromLatin1(hash.result().toHex()) == expectedHex.toLower();
}

}  // namespace

PrepareHorizonResources::PrepareHorizonResources(LaunchTask* parent) : LaunchStep(parent) {}

MinecraftInstance* PrepareHorizonResources::instance() const
{
    return m_parent->instance();
}

void PrepareHorizonResources::executeTask()
{
    m_buildDir = Horizon::buildDir(instance());

    if (!QFileInfo::exists(Horizon::prepareExecutable(m_buildDir)) || !QFileInfo::exists(Horizon::clientExecutable(m_buildDir))) {
        const char* reason =
            QT_TR_NOOP("This instance's Horizon build is missing. Delete the instance and create it again to reinstall Horizon.");
        emit logLine(tr(reason), MessageLevel::Fatal);
        emitFailed(tr(reason));
        return;
    }

    emit logLine(tr("Checking Horizon's resources..."), MessageLevel::Launcher);
    runPrepareTool({ "--launch-api" }, Stage::Probe);
}

void PrepareHorizonResources::runPrepareTool(const QStringList& arguments, Stage stage)
{
    m_stage = stage;
    m_prepareStdErr.clear();
    m_prepareOutBuffer.clear();
    m_prepareError.clear();

    m_process = new QProcess(this);
    m_process->setWorkingDirectory(m_buildDir);
    connect(m_process, &QProcess::finished, this, &PrepareHorizonResources::onToolFinished);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        // A process that fails to start never emits finished(); without this
        // the launch would wait here forever.
        if (error == QProcess::FailedToStart)
            emitFailed(tr("horizon-prepare could not be started. The Horizon build in this instance may be broken."));
    });
    if (stage == Stage::Prepare) {
        connect(m_process, &QProcess::readyReadStandardOutput, this, &PrepareHorizonResources::readPrepareOutput);
        connect(m_process, &QProcess::readyReadStandardError, this,
                [this] { m_prepareStdErr += QString::fromUtf8(m_process->readAllStandardError()); });
    }
    m_process->start(Horizon::prepareExecutable(m_buildDir), arguments);
}

void PrepareHorizonResources::onToolFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!isRunning())
        return;

    const Stage stage = m_stage;
    const QByteArray stdOut = m_process ? m_process->readAllStandardOutput() : QByteArray();
    auto* process = m_process;
    m_process = nullptr;
    if (process)
        process->deleteLater();

    switch (stage) {
        case Stage::Probe: {
            if (status != QProcess::NormalExit || exitCode != 0) {
                emitFailed(tr("The Horizon build in this instance could not be read (horizon-prepare --launch-api failed)."));
                return;
            }
            const auto info = Horizon::parseLaunchApi(stdOut);
            if (!info || info->minecraft.isEmpty()) {
                emitFailed(tr("The Horizon build in this instance did not say which Minecraft version it needs."));
                return;
            }
            m_info = *info;
            emit logLine(tr("Horizon %1 plays Minecraft %2.").arg(m_info.horizon, m_info.minecraft), MessageLevel::Launcher);
            runPrepareTool({ "--check", "--output", Horizon::preparedDir(m_buildDir) }, Stage::Check);
            return;
        }
        case Stage::Check: {
            if (status == QProcess::NormalExit && exitCode == 0) {
                emit logLine(tr("Horizon's resources are ready."), MessageLevel::Launcher);
                emitSucceeded();
                return;
            }
            // The check prints one line per gap; showing them explains why
            // the first launch takes a while.
            for (const auto& line : QString::fromUtf8(stdOut).split('\n')) {
                if (!line.trimmed().isEmpty())
                    emit logLine(line.trimmed(), MessageLevel::Launcher);
            }
            emit logLine(tr("Preparing Horizon's resources: this happens once and can take a few minutes."), MessageLevel::Launcher);
            ensureGameFiles();
            return;
        }
        case Stage::Prepare: {
            if (status == QProcess::NormalExit && exitCode == 0) {
                setProgress(1000, 1000);
                emit logLine(tr("Horizon's resources are ready."), MessageLevel::Launcher);
                emitSucceeded();
                return;
            }
            if (!m_prepareError.isEmpty()) {
                emitFailed(tr("Horizon could not prepare its resources: %1").arg(m_prepareError));
            } else {
                const auto lines = m_prepareStdErr.split('\n', Qt::SkipEmptyParts);
                emitFailed(tr("Horizon could not prepare its resources (horizon-prepare left with code %1).%2")
                               .arg(exitCode)
                               .arg(lines.isEmpty() ? QString() : "\n" + lines.last()));
            }
            return;
        }
    }
}

void PrepareHorizonResources::ensureGameFiles()
{
    auto inst = instance();
    emit logLine(tr("Downloading the Minecraft %1 files...").arg(m_info.minecraft), MessageLevel::Launcher);

    m_filesTask = makeShared<SequentialTask>();
    m_filesTask->addTask(makeShared<MinecraftLoadAndCheck>(inst, Net::Mode::Online));
    for (const auto& task : inst->createUpdateTask())
        m_filesTask->addTask(task);

    connect(m_filesTask.get(), &Task::finished, this, [this] {
        auto inst = instance();
        if (!m_filesTask->wasSuccessful()) {
            emitFailed(tr("The Minecraft files Horizon needs could not be downloaded: %1").arg(m_filesTask->failReason()));
            return;
        }

        auto* components = inst->getPackProfile();
        if (components->getComponentVersion("net.minecraft") != m_info.minecraft && !m_versionFixed) {
            // The instance drifted away from the build's pinned version (its
            // Version page allows that). Preparation only works with the
            // pinned one, so put it back and fetch its files instead.
            m_versionFixed = true;
            emit logLine(tr("This Horizon build needs Minecraft %1; switching the instance to it.").arg(m_info.minecraft),
                         MessageLevel::Launcher);
            if (!components->setComponentVersion("net.minecraft", m_info.minecraft, true)) {
                emitFailed(tr("Could not switch the instance to Minecraft %1.").arg(m_info.minecraft));
                return;
            }
            components->saveNow();
            ensureGameFiles();
            return;
        }

        fetchMojangFiles();
    });
    m_filesTask->start();
}

void PrepareHorizonResources::fetchMojangFiles()
{
    emit logLine(tr("Fetching Mojang's version data for Minecraft %1...").arg(m_info.minecraft), MessageLevel::Launcher);

    const QUrl manifestUrl("https://piston-meta.mojang.com/mc/game/version_manifest_v2.json");
    auto [download, data] = Net::Download::makeByteArray(manifestUrl);
    m_netJob.reset(new NetJob(tr("Horizon version data"), APPLICATION->network()));
    m_netJob->addNetAction(download);
    connect(m_netJob.get(), &NetJob::failed, this,
            [this](const QString& reason) { emitFailed(tr("Mojang's version manifest could not be downloaded: %1").arg(reason)); });
    connect(m_netJob.get(), &NetJob::succeeded, this, [this, data] {
        m_manifestData = *data;
        m_netJob.reset();
        fetchVersionJson();
    });
    m_netJob->start();
}

void PrepareHorizonResources::fetchVersionJson()
{
    QUrl versionUrl;
    const auto manifest = QJsonDocument::fromJson(m_manifestData).object();
    for (const auto& entry : manifest.value("versions").toArray()) {
        const auto version = entry.toObject();
        if (version.value("id").toString() == m_info.minecraft) {
            versionUrl = QUrl(version.value("url").toString());
            break;
        }
    }
    if (versionUrl.isEmpty()) {
        emitFailed(tr("Mojang's version manifest has no entry for Minecraft %1.").arg(m_info.minecraft));
        return;
    }

    auto [download, data] = Net::Download::makeByteArray(versionUrl);
    m_netJob.reset(new NetJob(tr("Horizon version data"), APPLICATION->network()));
    m_netJob->addNetAction(download);
    connect(m_netJob.get(), &NetJob::failed, this, [this](const QString& reason) {
        emitFailed(tr("Mojang's data for Minecraft %1 could not be downloaded: %2").arg(m_info.minecraft, reason));
    });
    connect(m_netJob.get(), &NetJob::succeeded, this, [this, data] {
        m_versionJsonData = *data;
        m_netJob.reset();
        downloadJars();
    });
    m_netJob->start();
}

void PrepareHorizonResources::downloadJars()
{
    const auto versionJson = QJsonDocument::fromJson(m_versionJsonData).object();
    const auto downloads = versionJson.value("downloads").toObject();
    const auto clientDownload = downloads.value("client").toObject();
    const auto serverDownload = downloads.value("server").toObject();
    if (clientDownload.isEmpty() || serverDownload.isEmpty()) {
        emitFailed(tr("Mojang's data for Minecraft %1 does not list the client and server jars Horizon needs.").arg(m_info.minecraft));
        return;
    }

    const QString files = Horizon::filesDir(m_buildDir);
    if (!FS::ensureFolderPathExists(files)) {
        emitFailed(tr("Could not create the folder for Horizon's Minecraft files."));
        return;
    }

    m_versionJsonPath = FS::PathCombine(files, m_info.minecraft + ".json");
    FS::write(m_versionJsonPath, m_versionJsonData);
    if (!QFileInfo::exists(m_versionJsonPath)) {
        emitFailed(tr("Could not save Mojang's version data for Minecraft %1.").arg(m_info.minecraft));
        return;
    }

    // The client jar usually already sits in the shared libraries, downloaded
    // like for any other instance; only fetch it again when it does not.
    m_clientJar.clear();
    if (auto profile = instance()->getPackProfile()->getProfile(); profile && profile->getMainJar()) {
        QStringList jars, natives, natives32, natives64;
        profile->getMainJar()->getApplicableFiles(instance()->runtimeContext(), jars, natives, natives32, natives64,
                                                  instance()->getLocalLibraryPath());
        const QString mainJar = jars.value(0);
        if (!mainJar.isEmpty() && QFileInfo::exists(mainJar))
            m_clientJar = mainJar;
    }

    m_serverJar = FS::PathCombine(files, "server.jar");
    const QString serverSha1 = serverDownload.value("sha1").toString();
    const bool serverJarOk = !serverSha1.isEmpty() && fileMatchesChecksum(m_serverJar, QCryptographicHash::Sha1, serverSha1);

    m_netJob.reset(new NetJob(tr("Horizon game files"), APPLICATION->network()));
    if (m_clientJar.isEmpty()) {
        m_clientJar = FS::PathCombine(files, m_info.minecraft + "-client.jar");
        auto download = Net::Download::makeFile(QUrl(clientDownload.value("url").toString()), m_clientJar);
        download->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha1, clientDownload.value("sha1").toString()));
        m_netJob->addNetAction(download);
    }
    if (!serverJarOk) {
        // The server jar is not part of any ordinary instance; Horizon
        // prepares from it, so it is fetched here like the Horizon launcher
        // does: only ever from Mojang, only for the owner's version.
        auto download = Net::Download::makeFile(QUrl(serverDownload.value("url").toString()), m_serverJar);
        download->addValidator(new Net::ChecksumValidator(QCryptographicHash::Sha1, serverSha1));
        m_netJob->addNetAction(download);
    }

    if (m_netJob->size() == 0) {
        m_netJob.reset();
        startPrepare();
        return;
    }

    connect(m_netJob.get(), &NetJob::stepProgress, this, &PrepareHorizonResources::propagateStepProgress);
    connect(m_netJob.get(), &NetJob::failed, this,
            [this](const QString& reason) { emitFailed(tr("A game file Horizon needs could not be downloaded: %1").arg(reason)); });
    connect(m_netJob.get(), &NetJob::succeeded, this, [this] {
        m_netJob.reset();
        startPrepare();
    });
    m_netJob->start();
}

void PrepareHorizonResources::startPrepare()
{
    for (const char* option : { "--prepare", "--complete", "--progress", "--version-json", "--client-jar", "--libraries-dir",
                                "--assets-dir", "--server-jar", "--output" }) {
        if (!m_info.accepts(option)) {
            emitFailed(tr("This Horizon build's horizon-prepare does not offer %1, which this launcher needs to prepare the game. "
                          "The build may be too old or too new for this version of Fjord Launcher.")
                           .arg(option));
            return;
        }
    }

    auto inst = instance();
    QStringList arguments{ "--prepare",
                           "--server-jar",
                           m_serverJar,
                           "--version-json",
                           m_versionJsonPath,
                           "--client-jar",
                           m_clientJar,
                           "--libraries-dir",
                           inst->librariesPath().absolutePath(),
                           "--assets-dir",
                           QDir("assets").absolutePath(),
                           "--output",
                           Horizon::preparedDir(m_buildDir),
                           "--complete",
                           "--progress",
                           "json" };
    if (m_info.accepts("--java-home")) {
        if (const QString home = javaHome(); !home.isEmpty())
            arguments << "--java-home" << home;
    }

    emit logLine(tr("Preparing Horizon's resources from your Minecraft %1 files (this only happens once)...").arg(m_info.minecraft),
                 MessageLevel::Launcher);
    runPrepareTool(arguments, Stage::Prepare);
}

void PrepareHorizonResources::readPrepareOutput()
{
    if (!m_process)
        return;

    m_prepareOutBuffer += m_process->readAllStandardOutput();
    int newline;
    while ((newline = m_prepareOutBuffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_prepareOutBuffer.left(newline).trimmed();
        m_prepareOutBuffer.remove(0, newline + 1);
        if (line.isEmpty())
            continue;

        const auto doc = QJsonDocument::fromJson(line);
        if (!doc.isObject()) {
            emit logLine(QString::fromUtf8(line), MessageLevel::Launcher);
            continue;
        }
        const auto object = doc.object();
        if (object.contains("error")) {
            m_prepareError = object.value("error").toString();
        } else if (object.contains("fraction")) {
            setProgress(static_cast<qint64>(object.value("fraction").toDouble() * 1000), 1000);
        } else if (object.contains("message")) {
            emit logLine(object.value("message").toString(), MessageLevel::Launcher);
        }
    }
}

QString PrepareHorizonResources::javaHome() const
{
    const QString javaPath = instance()->settings()->get("JavaPath").toString();
    if (javaPath.isEmpty())
        return {};

    QDir home = QFileInfo(javaPath).dir();  // "<home>/bin"
    if (!home.cdUp())
        return {};
    if (QFileInfo::exists(home.filePath("bin/java")) || QFileInfo::exists(home.filePath("bin/java.exe")))
        return home.absolutePath();
    return {};
}

bool PrepareHorizonResources::abort()
{
    if (m_process)
        m_process->kill();
    if (m_filesTask)
        m_filesTask->abort();
    if (m_netJob)
        m_netJob->abort();
    return true;
}
