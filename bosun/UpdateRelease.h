// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>
#include <optional>

namespace bosun {
struct UpdatePlatform {
    QString system;
    QString architecture;
    QString distribution;
    QString distributionVersion;
    bool appImage = false;
};
struct UpdateRelease {
    QString tag;
    QString version;
    QUrl notes;
    QUrl download;
    QString assetName;
};
inline constexpr qsizetype MaxReleaseBytes = qsizetype(1024) * 1024;
// Stable release comparison: build metadata does not affect precedence. An
// installed prerelease is older than the stable release with the same numbers.
[[nodiscard]] std::optional<bool> newerStableVersion(const QString &installed, const QString &candidate);
[[nodiscard]] std::optional<UpdateRelease> parseUpdateRelease(const QByteArray &json, const UpdatePlatform &platform);
[[nodiscard]] QByteArray updateReleaseCache(const UpdateRelease &release);
[[nodiscard]] UpdatePlatform currentUpdatePlatform();
} // namespace bosun
