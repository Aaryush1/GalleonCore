// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QFile>
namespace galleond {
// A configuration that can launch applications must not be writable by other users.
[[nodiscard]] bool isTrustedPagesFile(const QFile &file);
}
