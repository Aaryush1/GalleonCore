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
// Initial discovery is capped at 12 launchers; no generated pagination.
[[nodiscard]] QJsonObject seedWindowsLaunchers(QJsonObject root, const QList<DesktopApp> &apps, const QList<SteamGame> &games);
// No discovery: existing keys/empty slots stay fixed, including on reload and edit.
[[nodiscard]] QJsonObject fixedWindowsPages(QJsonObject root);
// Validates and freezes Windows auto-fill. Linux returns the original bytes unchanged.
[[nodiscard]] QByteArray preparePagesJson(const QByteArray &bytes);
[[nodiscard]] QStringList windowsAppCategories(const QString &name, const QString &folder);
}
