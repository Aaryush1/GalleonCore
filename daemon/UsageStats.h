// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QDateTime>
#include <QHash>
#include <QString>

namespace galleond {

// How much each application (or game) is used from the module, to put the most used first.
//
// Each launch counts 1 and fades with time: a launch is worth half after
// HalfLifeDays, a quarter after twice that. Only the running total and the date of
// the last launch are kept per application (score' = score * 0.5^(days / half-life) + 1),
// so the file stays small however long it is used. It holds at most MaxEntries
// applications (the least used are forgotten first) and is written atomically.
//
// The file is the user's own data and is read as untrusted: size, structure and every
// value are checked, and anything wrong is ignored.
class UsageStats
{
public:
    static constexpr double HalfLifeDays = 30.0;
    static constexpr int MaxEntries = 200;
    static constexpr int MaxIdLength = 200;
    static constexpr qint64 MaxFileBytes = qint64{256} * 1024;

    // `file` is read now and written at each record(); empty: memory only (tests).
    explicit UsageStats(QString file = {});

    // One launch of `id` (a desktop file ID, or "steam:<appid>").
    void record(const QString &id, const QDateTime &now = QDateTime::currentDateTimeUtc());

    // The faded score of `id` at `now`; 0 for an application never launched.
    [[nodiscard]] double score(const QString &id, const QDateTime &now = QDateTime::currentDateTimeUtc()) const;

    [[nodiscard]] qsizetype size() const { return m_entries.size(); }

    // The fading used everywhere (also for the Steam play time): 0.5^(days / HalfLifeDays).
    [[nodiscard]] static double fade(qint64 fromSecs, qint64 toSecs);

private:
    struct Entry
    {
        double score = 0;
        qint64 at = 0; // seconds since the epoch, UTC
    };

    void load();
    [[nodiscard]] bool save() const;
    void prune(qint64 nowSecs);

    QString m_file;
    QHash<QString, Entry> m_entries;
};

} // namespace galleond
