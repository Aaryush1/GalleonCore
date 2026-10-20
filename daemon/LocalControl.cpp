// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "LocalControl.h"

#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <QTimer>
#include <QStandardPaths>
#include <QVariant>
#ifdef Q_OS_WIN
#include <Windows.h>
#include <sddl.h>
#include <QScopeGuard>
#include <vector>
#include <cstddef>
#endif

namespace galleond {
namespace {
constexpr qint64 MaxFrame = 4096;
constexpr int DeadlineMs = 1500;
constexpr int MaxConnections = 4;
}

std::optional<LocalCommand> parseLocalCommand(const QByteArray &frame)
{
    if (frame.isEmpty() || frame.size() > MaxFrame)
        return std::nullopt;
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(frame, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
        return std::nullopt;
    const QJsonObject object = document.object();
    const QString action = object.value(QStringLiteral("action")).toString();
    if (object.size() == 1 && action == QStringLiteral("ping"))
        return LocalCommand{LocalAction::Ping, {}};
    if (object.size() == 1 && action == QStringLiteral("stop"))
        return LocalCommand{LocalAction::Stop, {}};
    if (object.size() != 2 || action != QStringLiteral("wallpaper"))
        return std::nullopt;
    const QJsonValue value = object.value(QStringLiteral("name"));
    if (!value.isString())
        return std::nullopt;
    const QString name = value.toString();
    // The catalog performs the final lookup. No path or process is executed here.
    if (name.isEmpty() || name.size() > 1024 || name.contains(QChar::Null))
        return std::nullopt;
    return LocalCommand{LocalAction::Wallpaper, name};
}

QByteArray encodeLocalCommand(const LocalCommand &command)
{
    QJsonObject object;
    switch (command.action) {
    case LocalAction::Ping: object.insert(QStringLiteral("action"), QStringLiteral("ping")); break;
    case LocalAction::Stop: object.insert(QStringLiteral("action"), QStringLiteral("stop")); break;
    case LocalAction::Wallpaper:
        object.insert(QStringLiteral("action"), QStringLiteral("wallpaper"));
        object.insert(QStringLiteral("name"), command.wallpaper);
        break;
    }
    return QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
}

QString localControlName()
{
#ifdef Q_OS_WIN
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token))
        return {};
    const auto closeToken = qScopeGuard([token] { CloseHandle(token); });
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    if (size == 0 || size > 65536)
        return {};
    std::vector<std::byte> storage(size);
    if (!GetTokenInformation(token, TokenUser, storage.data(), size, &size))
        return {};
    const auto *user = reinterpret_cast<const TOKEN_USER *>(storage.data());
    LPWSTR sid = nullptr;
    if (!ConvertSidToStringSidW(user->User.Sid, &sid))
        return {};
    const auto freeSid = qScopeGuard([sid] { LocalFree(sid); });
    const QByteArray identity = QString::fromWCharArray(sid).toUtf8();
#else
    const QByteArray identity = QDir::homePath().toUtf8();
#endif
    const QByteArray digest = QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex().left(24);
    return QStringLiteral("galleoncore-") + QString::fromLatin1(digest);
}

LocalControl::LocalControl(QObject *parent) : QObject(parent)
{
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    m_server.setMaxPendingConnections(MaxConnections);
    connect(&m_server, &QLocalServer::newConnection, this, &LocalControl::acceptConnections);
}

bool LocalControl::listen(const QString &name)
{
    if (name.isEmpty() || m_server.isListening()) {
        m_error = QStringLiteral("Invalid endpoint or server already listening");
        return false;
    }
    const QString folder = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/galleoncore");
    if (!QDir().mkpath(folder)) {
        m_error = QStringLiteral("Cannot create the local control lock directory");
        return false;
    }
    const QString digest = QString::fromLatin1(QCryptographicHash::hash(name.toUtf8(), QCryptographicHash::Sha256).toHex());
    m_lock = std::make_unique<QLockFile>(folder + QStringLiteral("/ipc-") + digest + QStringLiteral(".lock"));
    m_lock->setStaleLockTime(0);
    if (!m_lock->tryLock(0)) {
        m_error = QStringLiteral("Another instance owns the local control endpoint");
        return false;
    }
    if (!m_server.listen(name)) {
        m_error = m_server.errorString();
        m_lock.reset();
        return false;
    }
    m_error.clear();
    return true;
}

QString LocalControl::errorString() const
{
    return m_error;
}

void LocalControl::acceptConnections()
{
    while (m_server.hasPendingConnections()) {
        QLocalSocket *const socket = m_server.nextPendingConnection();
        if (!socket)
            continue;
        socket->setParent(this);
        if (m_connections >= MaxConnections) {
            socket->abort();
            socket->deleteLater();
            continue;
        }
        ++m_connections;
        socket->setReadBufferSize(MaxFrame + 1);
        connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QObject::destroyed, this, [this] { --m_connections; });
        QTimer::singleShot(DeadlineMs, socket, [socket] {
            socket->abort();
            socket->deleteLater();
        });
        const auto readFrame = [this, socket] {
            if (socket->property("handled").toBool())
                return;
            if (socket->bytesAvailable() > MaxFrame) {
                socket->abort();
                return;
            }
            if (!socket->canReadLine())
                return;
            const auto command = parseLocalCommand(socket->readLine(MaxFrame + 1));
            if (!command) {
                socket->abort();
                return;
            }
            // Consume a single request, disconnect before any callback can
            // enter a nested event loop and accidentally process it twice.
            socket->setProperty("handled", true);
            socket->write("{\"ok\":true}\n");
            socket->disconnectFromServer();
            switch (command->action) {
            case LocalAction::Ping: break;
            case LocalAction::Wallpaper: Q_EMIT wallpaperRequested(command->wallpaper); break;
            case LocalAction::Stop: Q_EMIT stopRequested(); break;
            }
        };
        connect(socket, &QLocalSocket::readyRead, socket, readFrame);
        readFrame(); // Data may already be buffered when the connection is accepted.
    }
}

void sendLocalCommand(const QString &name, const LocalCommand &command, QObject *owner)
{
    auto *socket = new QLocalSocket(owner);
    const QByteArray frame = encodeLocalCommand(command);
    QObject::connect(socket, &QLocalSocket::connected, socket, [socket, frame] {
        socket->write(frame);
        socket->disconnectFromServer();
    });
    QObject::connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
    QObject::connect(socket, &QLocalSocket::errorOccurred, socket, &QObject::deleteLater);
    QTimer::singleShot(DeadlineMs, socket, [socket] { socket->abort(); socket->deleteLater(); });
    socket->connectToServer(name);
}
} // namespace galleond
