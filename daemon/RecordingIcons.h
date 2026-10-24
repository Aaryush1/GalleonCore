// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QImage>
#include <QString>
namespace galleond {
// Built-in vector glyphs, shared by Bosun and the physical pad. No icon theme required.
[[nodiscard]] QImage recordingIcon(const QString &name, int size);
}
