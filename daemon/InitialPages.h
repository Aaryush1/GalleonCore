// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <QByteArray>
#include <QStringList>

namespace galleond {
// Used only when no user pages file exists. Never replaces a customized file.
[[nodiscard]] QByteArray initialPagesJson();
[[nodiscard]] QByteArray windowsInitialPagesJson(const QString &systemFolder, const QString &screensaverFolder);
[[nodiscard]] QStringList windowsAppCategories(const QString &name, const QString &folder);
}
