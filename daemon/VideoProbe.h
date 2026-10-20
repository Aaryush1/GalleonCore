// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QProcess>
#include <QQueue>
#include <QString>
#include <QTimer>

namespace galleond {

// What ffprobe says of the first video stream of a file.
struct VideoInfo
{
    bool valid = false;
    int width = 0;
    int height = 0;
    double fps = 0;
    double seconds = 0; // 0 when the file does not say
    QString codec;
};

// Reads the size, the frame rate and the length of local videos with ffprobe, for
// the settings window. The files are chosen by the user, and ffprobe is still
// started like FFmpeg is for thumbnails: no shell (an argument list), the lowest
// priority, at most TimeoutMs per file, only the `file` protocol, one file at a time,
// and an answer that is too long or does not hold plausible numbers counts as none.
class VideoProbe : public QObject
{
    Q_OBJECT

public:
    static constexpr int TimeoutMs = 10000;
    static constexpr qint64 MaxOutputBytes = qint64{64} * 1024;
    static constexpr int MaxQueued = 32;
    static constexpr int MaxDimension = 16384;
    static constexpr double MaxFps = 1000.0;
    static constexpr double MaxSeconds = 7.0 * 24 * 3600;

    // `ffprobe` is a program name or a path.
    explicit VideoProbe(const QString &ffprobe = QStringLiteral("ffprobe"), QObject *parent = nullptr);
    ~VideoProbe() override;

    [[nodiscard]] bool isAvailable() const { return !m_program.isEmpty(); }

    // Asks about `videoPath`; the answer comes by `probed`, an invalid VideoInfo when
    // there is none. A file already asked about and waiting is not queued twice.
    void request(const QString &videoPath);

    // The part that needs no process: ffprobe's JSON (`-of json`) to what it says.
    // Invalid when the text is not JSON or holds no sane video stream.
    [[nodiscard]] static VideoInfo parse(const QByteArray &json);

Q_SIGNALS:
    void probed(const QString &videoPath, const galleond::VideoInfo &info);

private:
    void startNext();
    void finish(const QString &videoPath, const QByteArray &output, bool ok);

    QString m_program;
    QString m_nice;
    QQueue<QString> m_queue;
    QString m_current;
    QProcess *m_process = nullptr;
    QTimer m_timeout;
};

} // namespace galleond

Q_DECLARE_METATYPE(galleond::VideoInfo)
