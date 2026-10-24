// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PackageUpdates.h"
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QStandardPaths>

namespace bosun {
namespace { constexpr qsizetype MaxOutput = qsizetype(64) * 1024; }
bool repositoryOffersUpdates(const QString &format, const QByteArray &output)
{
    if (output.size() > MaxOutput) return false;
    const QString text = QString::fromUtf8(output);
    if (format == QStringLiteral("deb")) {
        // apt-cache policy includes /var/lib/dpkg/status for a standalone DEB.
        // Only a positive-priority repository Packages index counts as managed.
        static const QRegularExpression repository(QStringLiteral("^\\s*[1-9][0-9]*\\s+(?:https?|ftp|file|copy|cdrom):[^\\n]+\\sPackages\\s*$"));
        for (const auto &line : text.split(QLatin1Char('\n')))
            if (repository.match(line).hasMatch()) return true;
    } else if (format == QStringLiteral("rpm")) {
        static const QRegularExpression repository(QStringLiteral("^GALLEON_UPDATE_REPO:[A-Za-z0-9_.:-]+\\z"));
        for (const auto &line : text.split(QLatin1Char('\n')))
            if (repository.match(line.trimmed()).hasMatch()) return true;
    }
    return false;
}
PackageUpdates::PackageUpdates(QObject *parent) : QObject(parent)
{
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] { finish(false); });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &PackageUpdates::consume);
    connect(&m_process, &QProcess::errorOccurred, this, [this] { finish(false); });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        consume();
        finish(code == 0 && status == QProcess::NormalExit);
    });
}
PackageUpdates::~PackageUpdates()
{
    m_pending = false;
    m_process.disconnect(this);
    if (m_process.state() != QProcess::NotRunning) { m_process.kill(); m_process.waitForFinished(1000); }
}
void PackageUpdates::start(const QString &format)
{
    if (m_pending || m_process.state() != QProcess::NotRunning) return;
    m_format = format;
    m_output.clear();
    QString program;
    QStringList arguments;
    if (format == QStringLiteral("deb")) {
        program = QStandardPaths::findExecutable(QStringLiteral("apt-cache"));
        arguments = {QStringLiteral("policy"), QStringLiteral("galleoncore")};
    } else if (format == QStringLiteral("rpm")) {
        program = QStandardPaths::findExecutable(QStringLiteral("dnf"));
        arguments = {QStringLiteral("--cacheonly"), QStringLiteral("repoquery"), QStringLiteral("--available"),
                     QStringLiteral("--queryformat"), QStringLiteral("GALLEON_UPDATE_REPO:%{repoid}\\n"), QStringLiteral("galleoncore")};
    }
    if (program.isEmpty()) { Q_EMIT detected(false, false); return; }
    auto environment = QProcessEnvironment::systemEnvironment();
    environment.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    m_process.setProcessEnvironment(environment);
    m_process.setStandardErrorFile(QProcess::nullDevice());
    m_pending = true;
    m_timeout.start(5000);
    m_process.start(program, arguments, QIODevice::ReadOnly);
}
void PackageUpdates::consume()
{
    if (!m_pending) return;
    m_output.append(m_process.read(MaxOutput - m_output.size() + 1));
    if (m_output.size() > MaxOutput || m_process.bytesAvailable() > 0) finish(false);
}
void PackageUpdates::finish(bool known)
{
    if (!m_pending) return;
    m_pending = false;
    m_timeout.stop();
    if (m_process.state() != QProcess::NotRunning) m_process.kill();
    Q_EMIT detected(known && repositoryOffersUpdates(m_format, m_output), known);
}
}
