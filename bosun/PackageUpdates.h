// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QProcess>
#include <QTimer>
#include <optional>

namespace bosun {
// A repository must actually offer this package; merely having APT/DNF installed
// or having installed a native package is not evidence that upgrades are managed.
[[nodiscard]] bool repositoryOffersUpdates(const QString &format, const QByteArray &output);
class PackageUpdates : public QObject {
    Q_OBJECT
public:
    explicit PackageUpdates(QObject *parent = nullptr);
    ~PackageUpdates() override;
    void start(const QString &format);
Q_SIGNALS:
    void detected(bool managed, bool known);
private:
    void finish(bool known);
    void consume();
    QProcess m_process;
    QTimer m_timeout;
    QString m_format;
    QByteArray m_output;
    bool m_pending = false;
};
}
