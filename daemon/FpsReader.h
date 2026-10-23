// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QDateTime>
#include <QString>
#include <optional>
namespace galleond {
struct FpsSample {
    std::optional<double> fps;
    QString status;
    QString process{};
    std::optional<double> cpuTemperature{};
    std::optional<double> gpuTemperature{};
    QString token{};
    std::optional<double> cpuLoad{};
    std::optional<double> gpuLoad{};
};
// Reads bounded head/tail windows of an actively written CSV, never a game process.
[[nodiscard]] FpsSample readFpsLog(const QString &source, const QString &process = {},
                                 const QDateTime &now = QDateTime::currentDateTime());
}
