// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QVariantMap>
#include <QStringList>
namespace galleond {
// Shared NVML ABI layout; the probe and fake library must use the same C++ type.
struct NvmlUtilization { unsigned int gpu; unsigned int memory; };
// Run these driver calls only in the isolated probe, never on the UI thread.
QVariantMap readNvmlSensors(const QStringList &libraries = {});
QVariantMap readAmdSmiSensors(const QStringList &libraries = {});
}
