// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QLocalServer>
#include <QLockFile>
#include <memory>
#include <QObject>
#include <QString>
#include <optional>

namespace galleond {

enum class LocalAction : unsigned char { Ping, Wallpaper, Stop };
struct LocalCommand
{
    LocalAction action = LocalAction::Ping;
    QString wallpaper;
};

// One bounded JSON object per connection; the server accepts only this user.
[[nodiscard]] std::optional<LocalCommand> parseLocalCommand(const QByteArray &frame);
[[nodiscard]] QByteArray encodeLocalCommand(const LocalCommand &command);
[[nodiscard]] QString localControlName();

class LocalControl final : public QObject
{
    Q_OBJECT
public:
    explicit LocalControl(QObject *parent = nullptr);
    [[nodiscard]] bool listen(const QString &name);
    [[nodiscard]] QString errorString() const;
Q_SIGNALS:
    void wallpaperRequested(const QString &name);
    void stopRequested();
private:
    void acceptConnections();
    std::unique_ptr<QLockFile> m_lock;
    QLocalServer m_server;
    QString m_error;
    int m_connections = 0;
};

// Event-loop driven and bounded by a deadline, so Bosun never blocks on IPC.
void sendLocalCommand(const QString &name, const LocalCommand &command, QObject *owner);

} // namespace galleond
