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

#include "HorizonInstanceCreationTask.h"

#include <QCryptographicHash>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QRegularExpression>
#include <QSysInfo>

#include "Application.h"
#include "FileSystem.h"
#include "QObjectPtr.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "net/Download.h"
#include "settings/INISettingsObject.h"

HorizonInstanceCreationTask::HorizonInstanceCreationTask(QString tag) : m_tag(std::move(tag)) {}

bool HorizonInstanceCreationTask::abort()
{
    if (m_netJob)
        m_netJob->abort();
    if (m_extractTask)
        m_extractTask->abort();
    if (m_probe)
        m_probe->kill();
    return InstanceTask::abort();
}

QString HorizonInstanceCreationTask::horizonDir() const
{
    return Horizon::buildDir(m_stagingPath);
}

QString HorizonInstanceCreationTask::prepareExecutable() const
{
    return Horizon::prepareExecutable(horizonDir());
}

void HorizonInstanceCreationTask::executeTask()
{
    m_archiveName = Horizon::archiveNameForPlatform();
    if (m_archiveName.isEmpty()) {
        emitFailed(tr("Horizon does not publish a build for this computer (%1 %2), so a Horizon instance cannot be created.")
                       .arg(QSysInfo::prettyProductName(), QSysInfo::currentCpuArchitecture()));
        return;
    }
    if (!m_tempDir.isValid()) {
        emitFailed(tr("Could not create a temporary folder to download Horizon into."));
        return;
    }

    setStatus(tr("Looking up the newest Horizon build..."));
    const QUrl url(QString("https://api.github.com/repos/%1/releases/tags/%2").arg(Horizon::Repo, m_tag));
    auto [download, data] = Net::Download::makeByteArray(url);

    m_netJob.reset(new NetJob(tr("Horizon build lookup"), APPLICATION->network()));
    m_netJob->addNetAction(download);
    connect(m_netJob.get(), &NetJob::succeeded, this, [this, data] {
        m_releaseInfoData = *data;
        releaseInfoDownloaded();
    });
    connect(m_netJob.get(), &NetJob::failed, this, &HorizonInstanceCreationTask::downloadFailed);
    m_netJob->start();
}

void HorizonInstanceCreationTask::releaseInfoDownloaded()
{
    m_netJob.reset();

    QJsonParseError error;
    const auto doc = QJsonDocument::fromJson(m_releaseInfoData, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject()) {
        emitFailed(tr("GitHub sent back an unreadable answer while looking for Horizon builds."));
        return;
    }

    const auto release = doc.object();
    m_releaseName = release.value("name").toString();
    for (const auto& assetValue : release.value("assets").toArray()) {
        const auto asset = assetValue.toObject();
        const auto name = asset.value("name").toString();
        const auto url = asset.value("browser_download_url").toString();
        if (name == m_archiveName)
            m_archiveUrl = QUrl(url);
        else if (name == "SHA256SUMS")
            m_sumsUrl = QUrl(url);
    }

    if (m_archiveUrl.isEmpty()) {
        // Nightlies occasionally miss a platform (the Intel macOS build, most
        // often); that is worth saying precisely instead of failing vaguely.
        emitFailed(tr("The latest Horizon build (%1) has no download for your system (%2). Sometimes a platform is missing from a "
                      "Nightly for a short while. Please try again later, or check the builds yourself:\n"
                      "https://github.com/%3/releases")
                       .arg(m_releaseName.isEmpty() ? m_tag : m_releaseName, m_archiveName, Horizon::Repo));
        return;
    }
    if (m_sumsUrl.isEmpty()) {
        emitFailed(tr("The latest Horizon build does not come with the checksums (SHA256SUMS) needed to verify the download."));
        return;
    }

    setStatus(tr("Downloading Horizon (%1)...").arg(m_archiveName));
    m_netJob.reset(new NetJob(tr("Horizon download"), APPLICATION->network()));
    m_netJob->addNetAction(Net::Download::makeFile(m_archiveUrl, m_tempDir.filePath(m_archiveName)));
    m_netJob->addNetAction(Net::Download::makeFile(m_sumsUrl, m_tempDir.filePath("SHA256SUMS")));
    connect(m_netJob.get(), &NetJob::succeeded, this, &HorizonInstanceCreationTask::buildDownloaded);
    connect(m_netJob.get(), &NetJob::failed, this, &HorizonInstanceCreationTask::downloadFailed);
    connect(m_netJob.get(), &NetJob::progress, this, [this](qint64 current, qint64 total) {
        if (total > 0)
            setProgress(current * 60 / total, 100);
    });
    connect(m_netJob.get(), &NetJob::stepProgress, this, &HorizonInstanceCreationTask::propagateStepProgress);
    m_netJob->start();
}

void HorizonInstanceCreationTask::buildDownloaded()
{
    m_netJob.reset();

    setStatus(tr("Checking the downloaded build..."));
    setProgress(62, 100);

    const QString archivePath = m_tempDir.filePath(m_archiveName);

    QString expectedSum;
    {
        QFile sumsFile(m_tempDir.filePath("SHA256SUMS"));
        if (!sumsFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            emitFailed(tr("Could not read the downloaded checksums."));
            return;
        }
        const auto text = QString::fromUtf8(sumsFile.readAll());
        for (const auto& line : text.split('\n')) {
            const auto parts = line.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
            QString entryName = parts.isEmpty() ? QString() : parts.last();
            entryName.remove('*');  // sha256sum marks binary files with a '*'
            if (parts.size() >= 2 && QFileInfo(entryName).fileName() == m_archiveName) {
                expectedSum = parts.first().toLower();
                break;
            }
        }
    }
    if (expectedSum.isEmpty()) {
        emitFailed(tr("The checksums of the latest Horizon build say nothing about %1.").arg(m_archiveName));
        return;
    }

    QString actualSum;
    {
        QFile archiveFile(archivePath);
        if (!archiveFile.open(QIODevice::ReadOnly)) {
            emitFailed(tr("Could not read the downloaded Horizon build."));
            return;
        }
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!hash.addData(&archiveFile)) {
            emitFailed(tr("Could not read the downloaded Horizon build."));
            return;
        }
        actualSum = QString::fromLatin1(hash.result().toHex());
    }
    if (actualSum != expectedSum) {
        emitFailed(
            tr("The downloaded Horizon build failed its checksum test and was discarded. That usually means the download was "
               "cut short; please try again."));
        return;
    }

    setStatus(tr("Unpacking Horizon..."));
    const QString extractDir = FS::PathCombine(m_stagingPath, "horizon-extract");
    m_extractTask = makeShared<MMCZip::ExtractZipTask>(archivePath, QDir(extractDir));
    connect(m_extractTask.get(), &Task::succeeded, this, &HorizonInstanceCreationTask::extractionFinished);
    connect(m_extractTask.get(), &Task::failed, this, &HorizonInstanceCreationTask::emitFailed);
    connect(m_extractTask.get(), &Task::aborted, this, &HorizonInstanceCreationTask::emitAborted);
    connect(m_extractTask.get(), &Task::progress, this, [this](qint64 current, qint64 total) {
        if (total > 0)
            setProgress(62 + current * 33 / total, 100);
    });
    m_extractTask->start();
}

void HorizonInstanceCreationTask::extractionFinished()
{
    m_extractTask.reset();

    const QString extractDir = FS::PathCombine(m_stagingPath, "horizon-extract");
    const QString buildRoot = Horizon::findBuildRoot(QDir(extractDir));
    if (buildRoot.isEmpty()) {
        QDir(extractDir).removeRecursively();
        emitFailed(tr("The unpacked Horizon build does not contain the game (horizon-client), so it cannot be used."));
        return;
    }

    if (QFileInfo::exists(horizonDir()))
        QDir(horizonDir()).removeRecursively();
    if (buildRoot != horizonDir() && !QDir().rename(buildRoot, horizonDir())) {
        QDir(extractDir).removeRecursively();
        emitFailed(tr("Could not move the unpacked Horizon build into place."));
        return;
    }
    QDir(extractDir).removeRecursively();

    if (!Horizon::makeProgramsExecutable(horizonDir())) {
        emitFailed(tr("The unpacked Horizon build is missing horizon-client or horizon-prepare, so it cannot be used."));
        return;
    }

    // Ask the build itself which Minecraft version it needs and what it is,
    // per the Launch API; guessing from the archive name would rot quickly.
    setStatus(tr("Asking the build what it needs..."));
    m_probe = new QProcess(this);
    m_probe->setWorkingDirectory(horizonDir());
    connect(m_probe, &QProcess::finished, this, &HorizonInstanceCreationTask::probeFinished);
    connect(m_probe, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            emitFailed(tr("The downloaded horizon-prepare could not be started. It may not run on this computer."));
    });
    m_probe->start(prepareExecutable(), { "--launch-api" });
}

void HorizonInstanceCreationTask::probeFinished(int exitCode)
{
    if (!isRunning())
        return;

    if (exitCode != 0) {
        emitFailed(tr("The downloaded horizon-prepare did not answer --launch-api. The build may be broken; please try again later."));
        return;
    }

    const auto info = Horizon::parseLaunchApi(m_probe->readAllStandardOutput());
    m_probe->deleteLater();
    m_probe = nullptr;

    if (!info || info->minecraft.isEmpty()) {
        emitFailed(tr("The downloaded horizon-prepare did not say which Minecraft version it needs, so the instance cannot be set up."));
        return;
    }

    if (!createInstanceFromBuild(*info))
        return;

    setProgress(100, 100);
    emitSucceeded();
}

bool HorizonInstanceCreationTask::createInstanceFromBuild(const Horizon::LaunchApiInfo& info)
{
    setStatus(tr("Creating the instance (for Minecraft %1)...").arg(info.minecraft));

    auto instanceSettings = std::make_unique<INISettingsObject>(FS::PathCombine(m_stagingPath, "instance.cfg"));
    MinecraftInstance instance(m_globalSettings, std::move(instanceSettings), m_stagingPath);

    instance.setName(name());
    if (!m_instIcon.isEmpty() && m_instIcon != "default")
        instance.setIconKey(m_instIcon);

    auto components = instance.getPackProfile();
    components->buildingFromScratch();
    if (!components->setComponentVersion("net.minecraft", info.minecraft, true)) {
        emitFailed(tr("Could not add Minecraft %1 to the instance.").arg(info.minecraft));
        return false;
    }

    {
        SettingsObject::Lock lock(instance.settings());
        instance.settings()->set("HorizonInstance", true);
        instance.settings()->set("HorizonBuild", m_tag);
        instance.settings()->set("HorizonVersion", info.horizon);
    }
    components->saveNow();
    return true;
}

void HorizonInstanceCreationTask::downloadFailed(const QString& reason)
{
    m_netJob.reset();
    emitFailed(reason);
}
