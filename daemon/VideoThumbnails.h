// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

namespace galleond {

// Makes a picture of a local video with FFmpeg, for the track screen when the
// player (VLC) gives no cover. Pictures are kept in a cache folder, so a video
// is decoded once.
//
// FFmpeg reads files chosen by the user's player, and is still handled with
// care: it is started without a shell (an argument list), at the lowest
// priority, for at most TimeoutMs, with only the `file` protocol allowed (a
// playlist or a crafted file cannot make it reach the network), on a file with
// a known video extension, and its output is accepted only if it decodes as a
// sane JPEG. The cache is bounded in count and in size.
class VideoThumbnails : public QObject
{
    Q_OBJECT

public:
    static constexpr int MaxEntries = 300;
    static constexpr qint64 MaxTotalBytes = 100LL * 1024 * 1024;
    static constexpr int TimeoutMs = 15000;
    static constexpr int Side = 512; // the longest side of a picture, in pixels

    // `cacheFolder` is created if needed. `ffmpeg` is a program name or path;
    // `useNice` runs it through nice(1) when that exists (tests turn it off).
    explicit VideoThumbnails(QString cacheFolder, const QString& ffmpeg = QStringLiteral("ffmpeg"), QObject *parent = nullptr);
    ~VideoThumbnails() override;

    [[nodiscard]] bool isAvailable() const { return !m_program.isEmpty(); }

    // By extension: mp4, mkv, webm, avi, mov, m4v, mpg, mpeg, wmv, flv, ogv.
    [[nodiscard]] static bool isVideoFile(const QString &path);

    // The picture of this video if it is already in the cache, else "". The
    // cache key is the path, the size and the date of the file: a replaced file
    // gets a new picture.
    [[nodiscard]] QString cached(const QString &videoPath) const;

    // Makes the picture in the background; the signal says where it is. Only the
    // latest request is kept while one is running. `durationSeconds` (0 when
    // unknown) picks the moment: a tenth of the way in, between 1 and 60 s.
    void request(const QString &videoPath, double durationSeconds);

Q_SIGNALS:
    void thumbnailReady(const QString &videoPath, const QString &pictureFile);
    // No picture could be made (the file is not a video FFmpeg reads, or it took too long).
    void thumbnailFailed(const QString &videoPath);

private:
    struct Job
    {
        QString video;
        double duration = 0;
    };

    [[nodiscard]] QString keyFile(const QString &videoPath) const;
    void start(const Job &job, double at);
    void finished(const Job &job, double at, int exitCode, QProcess::ExitStatus status);
    void next();
    void prune() const;

    QString m_folder;
    QString m_program;     // the resolved ffmpeg, empty when not found
    QString m_nice;        // nice(1), empty when not found
    QProcess *m_process = nullptr;
    QTimer m_timeout;
    bool m_busy = false;
    bool m_hasPending = false;
    Job m_pending;
    QString m_temporary; // the file FFmpeg writes, renamed once it is checked
};

} // namespace galleond
