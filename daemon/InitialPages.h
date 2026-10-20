// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QStringList>
#include <QJsonObject>
#include <QList>
#include "DesktopEntries.h"
#include "SteamLibrary.h"

namespace galleond {
// Used only when no user pages file exists. Never replaces a customized file.
[[nodiscard]] QByteArray initialPagesJson();
[[nodiscard]] QByteArray windowsInitialPagesJson(const QString &systemFolder, const QString &screensaverFolder);
// Pure expansion of automatic launcher themes; existing navigation is never replaced.
[[nodiscard]] QJsonObject paginateWindowsLaunchers(QJsonObject root, const QList<DesktopApp> &apps, const QList<SteamGame> &games);
// Validates before discovery. Linux returns the original bytes unchanged.
[[nodiscard]] QByteArray preparePagesJson(const QByteArray &bytes);
[[nodiscard]] QStringList windowsAppCategories(const QString &name, const QString &folder);
}
