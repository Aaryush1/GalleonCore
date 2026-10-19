// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "UsageStats.h"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>
#include <cmath>

namespace galleond {

UsageStats::UsageStats(QString file)
    : m_file(std::move(file))
{
    if (!m_file.isEmpty())
        load();
}

double UsageStats::fade(qint64 fromSecs, qint64 toSecs)
{
    const double days = static_cast<double>(std::max<qint64>(0, toSecs - fromSecs)) / 86400.0; // a date in the future counts as now
    return std::pow(0.5, days / HalfLifeDays);
}

void UsageStats::record(const QString &id, const QDateTime &now)
{
    if (id.isEmpty() || id.size() > MaxIdLength)
        return;
    const qint64 at = now.toSecsSinceEpoch();
    Entry &entry = m_entries[id];
    entry.score = entry.score * fade(entry.at, at) + 1.0;
    entry.at = at;
    prune(at);
    if (!m_file.isEmpty() && !save())
        qWarning() << "galleond: cannot write the usage file" << m_file;
}

double UsageStats::score(const QString &id, const QDateTime &now) const
{
    const auto it = m_entries.constFind(id);
    if (it == m_entries.constEnd())
        return 0.0;
    return it->score * fade(it->at, now.toSecsSinceEpoch());
}

void UsageStats::prune(qint64 nowSecs)
{
    while (m_entries.size() > MaxEntries) {
        auto worst = m_entries.begin();
        double worstScore = worst->score * fade(worst->at, nowSecs);
        for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
            const double s = it->score * fade(it->at, nowSecs);
            if (s < worstScore) {
                worst = it;
                worstScore = s;
            }
        }
        m_entries.erase(worst);
    }
}

void UsageStats::load()
{
    QFile file(m_file);
    if (!file.exists())
        return;
    if (!file.open(QIODevice::ReadOnly) || file.size() > MaxFileBytes) {
        qWarning() << "galleond: the usage file" << m_file << "cannot be read or is too large: ignored";
        return;
    }
    const QJsonDocument document = QJsonDocument::fromJson(file.read(MaxFileBytes + 1));
    if (!document.isObject() || document.object().value(QStringLiteral("version")).toInt() != 1) {
        qWarning() << "galleond: the usage file" << m_file << "is not valid: ignored";
        return;
    }
    const QJsonObject entries = document.object().value(QStringLiteral("entries")).toObject();
    const qint64 limit = QDateTime::currentSecsSinceEpoch() + 86400; // nothing from the future
    for (auto it = entries.begin(); it != entries.end() && m_entries.size() < MaxEntries; ++it) {
        const QJsonObject value = it.value().toObject();
        const double score = value.value(QStringLiteral("score")).toDouble(-1);
        const double at = value.value(QStringLiteral("at")).toDouble(-1);
        if (it.key().isEmpty() || it.key().size() > MaxIdLength || !std::isfinite(score) || score < 0 || score > 1e6
            || !std::isfinite(at) || at < 0 || at > static_cast<double>(limit))
            continue;
        m_entries.insert(it.key(), Entry{score, static_cast<qint64>(at)});
    }
}

bool UsageStats::save() const
{
    QJsonObject entries;
    for (auto it = m_entries.constBegin(); it != m_entries.constEnd(); ++it)
        entries.insert(it.key(), QJsonObject{{QStringLiteral("score"), it->score}, {QStringLiteral("at"), static_cast<double>(it->at)}});
    const QJsonObject root{{QStringLiteral("version"), 1}, {QStringLiteral("entries"), entries}};
    QDir().mkpath(QFileInfo(m_file).absolutePath());
    QSaveFile file(m_file);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    return file.commit();
}

} // namespace galleond
