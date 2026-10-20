// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileTrust.h"
#include <QFileInfo>
#include <unistd.h>
namespace galleond {
bool isTrustedPagesFile(const QFile &file)
{
    const QFileInfo info(file);
    return info.ownerId() == getuid() && !info.permissions().testFlag(QFileDevice::WriteOther);
}
}
