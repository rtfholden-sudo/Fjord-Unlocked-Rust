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

#include <QByteArray>
#include <QDir>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

class MinecraftInstance;

/**
 * Shared helpers for Horizon instances.
 *
 * Horizon (https://github.com/horizon-rust/horizon) is a from-scratch Rust
 * reimplementation of Minecraft: Java Edition. It is distributed as native
 * binaries through its "Launch API", which is the integration contract other
 * launchers are expected to follow:
 * https://github.com/horizon-rust/horizon/blob/main/docs/api/launch-guide.md
 *
 * A Horizon instance stores everything Horizon-related inside a "horizon"
 * folder in the instance root:
 *
 *   horizon/                 the unpacked build (horizon-client, horizon-prepare,
 *                            research-lock.json, exporters/)
 *   horizon/prepared/        resources produced by `horizon-prepare --prepare`
 *   horizon/minecraft/       Mojang inputs for preparation (version JSON,
 *                            server jar if the main jar was not downloaded)
 */
namespace Horizon {

/// GitHub repository the builds are published in.
inline constexpr const char* Repo = "horizon-rust/horizon";

/// Release tag that always points at the newest build.
inline constexpr const char* NightlyTag = "nightly";

/// File name of the archive published for this computer, e.g.
/// "horizon-linux-x86_64.tar.gz". Empty when Horizon ships no build for us.
QString archiveNameForPlatform();

/// Whether Horizon publishes a build for this computer at all.
bool isPlatformSupported();

/// Folder with the unpacked Horizon build of an instance.
QString buildDir(const QString& instanceRoot);
QString buildDir(const MinecraftInstance* instance);

/// Full path of the game binary ("horizon-client", .exe on Windows).
QString clientExecutable(const QString& buildDir);

/// Full path of the resource preparer ("horizon-prepare", .exe on Windows).
QString prepareExecutable(const QString& buildDir);

/// Folder with resources prepared by horizon-prepare ("--output").
QString preparedDir(const QString& buildDir);

/// Folder holding the Mojang files preparation reads (version JSON, server jar).
QString filesDir(const QString& buildDir);

/// Release download URL of an asset of a tagged release, e.g.
/// https://github.com/horizon-rust/horizon/releases/download/nightly/SHA256SUMS
QUrl assetUrl(const QString& tag, const QString& assetName);

/// Find the directory inside an extracted release archive that holds the
/// build, i.e. the one containing the game binary. Release archives may or
/// may not wrap everything in a single top-level folder. Returns an empty
/// string when no build was found.
QString findBuildRoot(const QDir& extractedRoot);

/// Make the build's programs executable (archives are not trusted to keep
/// the executable bit). Returns false if a program is missing.
bool makeProgramsExecutable(const QString& buildDir);

/**
 * The answer of `horizon-client --launch-api` / `horizon-prepare --launch-api`.
 * Only the fields Fjord needs are extracted; anything else is ignored, as
 * required by the Launch API contract.
 */
struct LaunchApiInfo {
    QString launchApi;    ///< value of "launch_api", e.g. "0.1.1"
    QString horizon;      ///< value of "horizon": the build's version
    QString minecraft;    ///< value of "minecraft": pinned Minecraft version
    int javaMajor = 0;    ///< value of "java_major" (prepare only)
    QStringList options;  ///< names of the options the program accepts

    bool accepts(const QString& option) const { return options.contains(option); }
};

std::optional<LaunchApiInfo> parseLaunchApi(const QByteArray& output);

}  // namespace Horizon
