// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QSet>
#include <QTimer>

namespace galleond {
// Resolves local app/Steam artwork in one isolated child at a time. No HID or launcher action.
// Bounded queue/cache/output and a hard deadline keep Shell/Steam I/O off the device event loop.
class LauncherCache final : public QObject
{
    Q_OBJECT
public:
    explicit LauncherCache(QString program = {}, int deadlineMs = 2500, QObject *parent = nullptr);
    ~LauncherCache() override;
    [[nodiscard]] QJsonObject lookup(const QString &reference);
    [[nodiscard]] bool contains(const QString &reference) const { return m_cache.contains(reference); }
    void clear();
    void cancelPending();
Q_SIGNALS:
    void ready(const QString &reference);
private:
    void next();
    void read();
    void finished(bool success);
    QString m_program;
    QString m_active;
    QQueue<QString> m_queue;
    QSet<QString> m_pending;
    QHash<QString, QJsonObject> m_cache;
    QProcess m_process;
    QTimer m_deadline;
    QByteArray m_output;
    qsizetype m_received = 0;
    bool m_bad = false;
};
}
