// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QString>

namespace bosun::filenames {

inline constexpr int MaxNameLength = 60;

// A file name from a title: letters, digits, spaces, "-", "_" and "."; anything else becomes
// "-"; no leading dot or space; at most MaxNameLength; "file" when nothing is left. Pure.
[[nodiscard]] QString safeName(const QString &wished, const QString &fallback = QStringLiteral("file"));

// `<folder>/<base><suffix>` (suffix like ".mp4"), or `<base>-2<suffix>`, `-3`... the first that
// is not there. Never a file that exists.
[[nodiscard]] QString freePath(const QString &folder, const QString &base, const QString &suffix);

} // namespace bosun::filenames
