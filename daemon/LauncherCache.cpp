// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LauncherCache.h"
#include <QCoreApplication>
#include <QDebug>
#include <QJsonDocument>
#include <utility>

namespace galleond {
namespace {
constexpr int MaxPending = 12;
constexpr int MaxCached = 64;
constexpr qsizetype MaxOutput = 64 * 1024;
}
LauncherCache::LauncherCache(QString program, int deadlineMs, QObject *parent)
    : QObject(parent), m_program(program.isEmpty() ? QCoreApplication::applicationFilePath() : std::move(program))
{
    m_deadline.setSingleShot(true);
    m_deadline.setInterval(qMax(1, deadlineMs));
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        m_bad = true;
        qWarning() << "galleond: launcher metadata deadline exceeded" << m_active;
        m_process.kill(); // Only this metadata child, never an application belonging to the user.
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &LauncherCache::read);
    connect(&m_process, &QProcess::readyReadStandardError, this, &LauncherCache::read);
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        finished(code == 0 && status == QProcess::NormalExit);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finished(false);
    });
}
LauncherCache::~LauncherCache()
{
    disconnect(&m_process, nullptr, this, nullptr);
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(1000);
    }
}
QJsonObject LauncherCache::lookup(const QString &reference)
{
    if (const auto found = m_cache.constFind(reference); found != m_cache.constEnd())
        return found.value();
    if (reference.isEmpty() || reference.size() > 4096 || reference.contains(QChar::Null)
        || m_pending.contains(reference) || m_pending.size() >= MaxPending)
        return {};
    m_pending.insert(reference);
    m_queue.enqueue(reference);
    next();
    return {};
}
void LauncherCache::clear()
{
    m_cache.clear();
    cancelPending();
}
void LauncherCache::cancelPending()
{
    m_queue.clear();
    m_pending.clear();
    m_active.clear();
    m_deadline.stop();
    if (m_process.state() != QProcess::NotRunning)
        m_process.kill();
}
void LauncherCache::next()
{
    if (!m_active.isEmpty() || m_queue.isEmpty() || m_process.state() != QProcess::NotRunning)
        return;
    m_active = m_queue.dequeue();
    m_output.clear();
    m_received = 0;
    m_bad = false;
    m_process.setProgram(m_program);
    m_process.setArguments({QStringLiteral("--launcher-info"), m_active});
    m_deadline.start();
    m_process.start();
}
void LauncherCache::read()
{
    const QByteArray data = m_process.readAllStandardOutput();
    const QByteArray errors = m_process.readAllStandardError();
    m_received += data.size() + errors.size();
    if (m_received > MaxOutput) {
        m_bad = true;
        m_process.kill();
    } else if (!m_bad) {
        m_output.append(data);
    }
}
void LauncherCache::finished(bool success)
{
    m_deadline.stop();
    read();
    const QString reference = std::exchange(m_active, {});
    if (!reference.isEmpty()) {
        QJsonObject result;
        const QJsonDocument document = QJsonDocument::fromJson(m_output);
        if (success && !m_bad && document.isObject()) {
            const auto object = document.object();
            const auto name = object.value(QStringLiteral("name"));
            const auto icon = object.value(QStringLiteral("icon"));
            if (name.isString() && name.toString().size() <= 80 && icon.isString() && icon.toString().size() <= 4096)
                result = QJsonObject{{QStringLiteral("name"), name}, {QStringLiteral("icon"), icon}};
        }
        if (m_cache.size() >= MaxCached)
            m_cache.erase(m_cache.begin());
        m_cache.insert(reference, result); // Negative results are cached too; no retry loop during redraw.
        m_pending.remove(reference);
        Q_EMIT ready(reference);
    }
    QTimer::singleShot(0, this, &LauncherCache::next);
}
}
