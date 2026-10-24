// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QString>
#include <QVariantMap>
#include <memory>
#include <optional>
namespace galleond {
QVariantMap presentMonInstallationFromImagePath(const QString &imagePath);
QVariantMap findPresentMonService();
QString startPresentMonService();
class PresentMonSession {
public:
    PresentMonSession();
    ~PresentMonSession();
    bool start(const QString &apiPath, quint32 pid);
    std::optional<double> poll();
    [[nodiscard]] QString error() const;
private:
    struct Impl;
    std::unique_ptr<Impl> m;
};
}
