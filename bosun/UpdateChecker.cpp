// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "UpdateChecker.h"
#include <QDateTime>
#include <QDir>
#include <QLocale>
#include <QNetworkRequest>
#include <QStandardPaths>
#include <algorithm>
#include <utility>

namespace bosun {
namespace {
constexpr qint64 DaySeconds = qint64(24) * 60 * 60;
constexpr qint64 ManualDelaySeconds = 60;
QString settingsPath(const QString &overridePath)
{
    return overridePath.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation)
        + QStringLiteral("/galleoncore/updates.ini") : overridePath;
}
// Bound persisted/server timestamps: clock corrections must not postpone checks forever.
qint64 boundedFuture(qint64 value, qint64 now)
{
    return value > now + DaySeconds ? now + DaySeconds : std::max<qint64>(0, value);
}
} // namespace

UpdateChecker::UpdateChecker(Options options, QObject *parent, QNetworkAccessManager *manager)
    : QObject(parent), m_options(std::move(options)), m_settings(settingsPath(m_options.settingsFile), QSettings::IniFormat),
      m_manager(manager ? manager : &m_network)
{
    if (simulation()) {
        m_automatic = false;
        m_status = tr("Simulation: check the latest real release as if version 0.0.0 were installed.");
    } else {
        m_automatic = !packageManaged() && m_settings.value(QStringLiteral("automatic"), true).toBool();
        const auto now = QDateTime::currentSecsSinceEpoch();
        m_lastAttempt = std::clamp(m_settings.value(QStringLiteral("lastAttempt"), 0).toLongLong(), qint64(0), now);
        m_retryAfter = boundedFuture(m_settings.value(QStringLiteral("retryAfter"), 0).toLongLong(), now);
        m_lastChecked = std::clamp(m_settings.value(QStringLiteral("lastChecked"), 0).toLongLong(), qint64(0), now);
        m_release = parseUpdateRelease(m_settings.value(QStringLiteral("release")).toByteArray(), m_options.platform);
        m_status = available() ? tr("A newer version is available (saved result).") : tr("Updates have not been checked in this session.");
        if (packageManaged()) m_status = tr("A configured repository supplies GalleonCore updates. Use your system package manager.");
    }
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] { fail(tr("The update check timed out. Try again later.")); });
    m_periodic.setInterval(60 * 60 * 1000);
    connect(&m_periodic, &QTimer::timeout, this, &UpdateChecker::checkAutomatically);
}
UpdateChecker::~UpdateChecker() { cancel(); }

QString UpdateChecker::lastChecked() const
{
    return m_lastChecked > 0 ? QLocale().toString(QDateTime::fromSecsSinceEpoch(m_lastChecked).toLocalTime(), QLocale::ShortFormat) : tr("Never");
}
bool UpdateChecker::available() const
{
    return !packageManaged() && m_release && newerStableVersion(simulation() ? QStringLiteral("0.0.0") : m_options.installedVersion, m_release->tag).value_or(false);
}
QString UpdateChecker::downloadLabel() const
{
    return m_release && !m_release->download.isEmpty() ? tr("Download %1").arg(m_release->assetName) : tr("Open release downloads");
}
bool UpdateChecker::packageManaged() const
{
    return !simulation() && m_options.packageManaged;
}
void UpdateChecker::setRepositoryManaged(bool managed, bool known)
{
    if (simulation()) return;
    m_options.packageManaged = managed;
    m_automatic = !managed && m_settings.value(QStringLiteral("automatic"), true).toBool();
    if (managed) m_status = tr("A configured repository supplies GalleonCore updates. Use your system package manager.");
    else if (known) m_status = tr("No repository supplies GalleonCore updates. Bosun can check GitHub.");
    else m_status = tr("Could not determine whether a repository supplies updates. You can check GitHub manually.");
    Q_EMIT changed();
}
void UpdateChecker::startAutomaticChecks()
{
    if (simulation() || packageManaged() || m_periodic.isActive()) return;
    m_periodic.start();
    // A fresh Bosun session checks the current release, even when another
    // session checked within the past day. check() still honours retryAfter.
    if (m_automatic) check(true);
}
void UpdateChecker::checkNow() { check(false); }
void UpdateChecker::checkAutomatically()
{
    if (!m_automatic) return;
    const auto now = QDateTime::currentSecsSinceEpoch();
    if (m_lastAttempt > 0 && now >= m_lastAttempt && now - m_lastAttempt < DaySeconds) return;
    check(true);
}
void UpdateChecker::setAutomatic(bool enabled)
{
    if (simulation() || packageManaged() || enabled == m_automatic) return;
    m_settings.setValue(QStringLiteral("automatic"), enabled);
    m_settings.sync();
    if (m_settings.status() != QSettings::NoError) {
        m_settings.setValue(QStringLiteral("automatic"), m_automatic);
        m_status = tr("Could not save the update preference.");
        Q_EMIT changed();
        return;
    }
    m_automatic = enabled;
    if (!enabled && m_automaticRequest && busy()) {
        cancel();
        m_status = tr("Automatic update check cancelled.");
    }
    Q_EMIT changed();
    if (enabled) check(true);
}
void UpdateChecker::check(bool automaticRequest)
{
    if (packageManaged() || busy()) return;
    const auto now = QDateTime::currentSecsSinceEpoch();
    if (now < m_retryAfter) {
        if (!automaticRequest) {
            m_status = tr("Please wait until %1 before checking again.").arg(QLocale().toString(QDateTime::fromSecsSinceEpoch(m_retryAfter).toLocalTime(), QLocale::ShortFormat));
            Q_EMIT changed();
        }
        return;
    }
    if (!newerStableVersion(m_options.installedVersion, QStringLiteral("0.0.0")).has_value()) {
        m_status = tr("This build has an unsupported version number. Check the project website.");
        Q_EMIT changed();
        return;
    }
    m_lastAttempt = now;
    m_retryAfter = now + ManualDelaySeconds;
    if (!simulation()) {
        m_settings.setValue(QStringLiteral("lastAttempt"), now);
        m_settings.setValue(QStringLiteral("retryAfter"), m_retryAfter);
        m_settings.sync();
    }
    m_automaticRequest = automaticRequest;
    m_body.clear();
    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/Paullux/GalleonCore/releases/latest")));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setRawHeader("X-GitHub-Api-Version", "2022-11-28");
    request.setRawHeader("User-Agent", "GalleonBosun/" + m_options.installedVersion.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(m_options.timeoutMs);
    request.setAttribute(QNetworkRequest::AutoDeleteReplyOnFinishAttribute, false);
    m_reply = m_manager->get(request);
    m_reply->setReadBufferSize(MaxReleaseBytes + 1);
    connect(m_reply, &QNetworkReply::readyRead, this, &UpdateChecker::consume);
    connect(m_reply, &QNetworkReply::finished, this, &UpdateChecker::finish);
    m_timeout.start(m_options.timeoutMs);
    m_status = tr("Checking GitHub for a newer stable release…");
    Q_EMIT changed();
}
void UpdateChecker::consume()
{
    if (!m_reply) return;
    const auto remaining = MaxReleaseBytes - m_body.size();
    m_body.append(m_reply->read(remaining + 1));
    if (m_body.size() > MaxReleaseBytes || m_reply->bytesAvailable() > 0)
        fail(tr("GitHub returned an oversized update response."));
}
void UpdateChecker::cancel()
{
    m_timeout.stop();
    if (m_reply) {
        auto *reply = std::exchange(m_reply, nullptr);
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    m_body.clear();
}
void UpdateChecker::fail(const QString &message)
{
    cancel();
    m_status = message;
    Q_EMIT changed();
}
void UpdateChecker::finish()
{
    consume();
    if (!m_reply) return;
    const auto http = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (http == 429 || (http == 403 && m_reply->rawHeader("X-RateLimit-Remaining") == "0")) {
        const auto now = QDateTime::currentSecsSinceEpoch();
        bool ok = false;
        auto delay = m_reply->rawHeader("Retry-After").toLongLong(&ok);
        if (!ok || delay < 1) delay = qint64(60) * 60;
        delay = std::clamp(delay, ManualDelaySeconds, DaySeconds);
        const auto reset = m_reply->rawHeader("X-RateLimit-Reset").toLongLong();
        m_retryAfter = std::max(now + delay, boundedFuture(reset, now));
        if (!simulation()) {
            m_settings.setValue(QStringLiteral("retryAfter"), m_retryAfter);
            m_settings.sync();
        }
        fail(tr("GitHub's request limit was reached. Please try again later."));
        return;
    }
    if (http == 404) { fail(tr("No stable release is published on GitHub yet.")); return; }
    if (http != 200 || m_reply->error() != QNetworkReply::NoError) {
        fail(tr("Could not contact GitHub. Check your connection and try again later."));
        return;
    }
    const auto release = parseUpdateRelease(m_body, m_options.platform);
    if (!release) { fail(tr("GitHub returned invalid release information.")); return; }
    m_release = release;
    m_lastChecked = QDateTime::currentSecsSinceEpoch();
    if (!simulation()) {
        m_settings.setValue(QStringLiteral("lastChecked"), m_lastChecked);
        m_settings.setValue(QStringLiteral("release"), updateReleaseCache(*release));
        m_settings.sync();
    }
    cancel();
    if (simulation()) m_status = tr("Simulation: real release %1 compared with a fictitious installed version 0.0.0.").arg(m_release->version);
    else m_status = available() ? tr("GalleonCore %1 is available.").arg(m_release->version) : tr("You are using the latest version or a newer build.");
    Q_EMIT changed();
}
void UpdateChecker::openDownload()
{
    if (m_release && available()) Q_EMIT openUrlRequested(m_release->download.isEmpty() ? m_release->notes : m_release->download);
}
void UpdateChecker::openReleaseNotes()
{
    if (m_release) Q_EMIT openUrlRequested(m_release->notes);
}
void UpdateChecker::openWebsite() { Q_EMIT openUrlRequested(QUrl(QStringLiteral("https://galleoncore.xyz/"))); }
} // namespace bosun
