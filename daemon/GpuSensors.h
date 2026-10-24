// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QVariantMap>
#include <QStringList>
namespace galleond {
// Run these driver calls only in the isolated probe, never on the UI thread.
QVariantMap readNvmlSensors(const QStringList &libraries = {});
QVariantMap readAmdSmiSensors(const QStringList &libraries = {});
}
