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

#include "HorizonSupport.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSysInfo>

#include "FileSystem.h"
#include "minecraft/MinecraftInstance.h"

namespace Horizon {

namespace {

QString programName(const QString& base)
{
#ifdef Q_OS_WIN
    return base + ".exe";
#else
    return base;
#endif
}

}  // namespace

QString archiveNameForPlatform()
{
    const QString arch = QSysInfo::currentCpuArchitecture();
#if defined(Q_OS_WIN)
    if (arch == "arm64")
        return "horizon-windows-arm64.zip";
    if (arch == "x86_64")
        return "horizon-windows-x64.zip";
#elif defined(Q_OS_MACOS)
    if (arch == "arm64")
        return "horizon-macos-arm64.zip";
    if (arch == "x86_64")
        return "horizon-macos-x64.zip";
#elif defined(Q_OS_LINUX)
    if (arch == "x86_64")
        return "horizon-linux-x86_64.tar.gz";
#endif
    return {};
}

bool isPlatformSupported()
{
    return !archiveNameForPlatform().isEmpty();
}

QString buildDir(const QString& instanceRoot)
{
    return FS::PathCombine(instanceRoot, "horizon");
}

QString buildDir(const MinecraftInstance* instance)
{
    return buildDir(instance->instanceRoot());
}

QString clientExecutable(const QString& buildDir)
{
    return FS::PathCombine(buildDir, programName("horizon-client"));
}

QString prepareExecutable(const QString& buildDir)
{
    return FS::PathCombine(buildDir, programName("horizon-prepare"));
}

QString preparedDir(const QString& buildDir)
{
    return FS::PathCombine(buildDir, "prepared");
}

QString filesDir(const QString& buildDir)
{
    return FS::PathCombine(buildDir, "minecraft");
}

QUrl assetUrl(const QString& tag, const QString& assetName)
{
    return QUrl(QString("https://github.com/%1/releases/download/%2/%3").arg(Repo, tag, assetName));
}

QString findBuildRoot(const QDir& extractedRoot)
{
    if (QFileInfo::exists(clientExecutable(extractedRoot.absolutePath())))
        return extractedRoot.absolutePath();

    const auto subdirs = extractedRoot.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name);
    for (const auto& subdir : subdirs) {
        if (subdir == "__MACOSX")
            continue;
        const QString candidate = extractedRoot.absoluteFilePath(subdir);
        if (QFileInfo::exists(clientExecutable(candidate)))
            return candidate;
    }
    return {};
}

bool makeProgramsExecutable(const QString& buildDir)
{
#ifdef Q_OS_WIN
    Q_UNUSED(buildDir);
    return true;
#else
    bool allPresent = true;
    for (const QString& program : { QStringLiteral("horizon-client"), QStringLiteral("horizon-prepare"), QStringLiteral("horizon-ctl"),
                                    QStringLiteral("horizon-server") }) {
        const QString path = FS::PathCombine(buildDir, program);
        if (!QFileInfo::exists(path)) {
            if (program == QStringLiteral("horizon-client") || program == QStringLiteral("horizon-prepare"))
                allPresent = false;
            continue;
        }
        auto permissions = QFile::permissions(path);
        permissions |= QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner;
        QFile::setPermissions(path, permissions);
    }
    return allPresent;
#endif
}

std::optional<LaunchApiInfo> parseLaunchApi(const QByteArray& output)
{
    // Both programs print exactly one JSON object; take the last line that
    // parses, in case anything else ended up on stdout.
    const auto lines = output.split('\n');
    for (int i = lines.size() - 1; i >= 0; --i) {
        const auto line = lines.at(i).trimmed();
        if (line.isEmpty())
            continue;

        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(line, &error);
        if (error.error != QJsonParseError::NoError || !doc.isObject())
            continue;

        const auto object = doc.object();
        if (!object.contains("launch_api"))
            continue;

        LaunchApiInfo info;
        info.launchApi = object.value("launch_api").toString();
        info.horizon = object.value("horizon").toString();
        info.minecraft = object.value("minecraft").toString();
        info.javaMajor = object.value("java_major").toInt();
        for (const auto& option : object.value("options").toArray()) {
            const auto name = option.toObject().value("name").toString();
            if (!name.isEmpty())
                info.options.append(name);
        }
        return info;
    }
    return std::nullopt;
}

}  // namespace Horizon
