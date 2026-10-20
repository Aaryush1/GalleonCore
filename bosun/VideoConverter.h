// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QObject>
#include <QProcess>
#include <QRect>
#include <QSize>
#include <QString>
#include <QTimer>

#include <optional>

namespace bosun {

// A video to cut to the picture of the module and write again.
struct ConversionJob
{
    QString input;        // an existing video file
    QSize source;         // its size in pixels (from ffprobe)
    QRect crop;           // the part kept, in the pixels of the video (see CropMath)
    QSize canvas;         // the size of the result
    int fps = 15;         // 1..20
    double seconds = 0;   // the length of the video, 0 when unknown (for the progress)
    QString outputFolder; // created when needed
    QString name;         // the name wished for the result, made safe
};

// Cuts a video with FFmpeg and writes it in a folder of the user's own, never over a file
// that is there. FFmpeg reads a file the user chose, so it is started with care: no shell
// (an argument list), the lowest priority, only the `file` protocol, no audio, subtitles or
// data copied, a result at most MaxSeconds long, a time limit, one conversion at a time, and
// the result is written under a temporary name and renamed once FFmpeg has finished well.
class VideoConverter : public QObject
{
    Q_OBJECT

public:
    static constexpr double MaxSeconds = 120.0;
    static constexpr int TimeoutMs = 30 * 60 * 1000;
    static constexpr int MaxNameLength = 60;

    // `ffmpeg` is a program name or a path.
    explicit VideoConverter(const QString &ffmpeg = QStringLiteral("ffmpeg"), QObject *parent = nullptr);
    ~VideoConverter() override;

    // The encoder that will be used ("libx264", else "mpeg4"); empty when FFmpeg is missing or has none.
    [[nodiscard]] QString encoder() const { return m_encoder; }
    [[nodiscard]] bool isAvailable() const { return !m_encoder.isEmpty(); }
    [[nodiscard]] bool isRunning() const { return m_process != nullptr; }

    // Starts the conversion in the background; "" when it started, else why not (nothing is started).
    [[nodiscard]] QString start(const ConversionJob &job);
    // Stops the conversion that is running and deletes what it had written. `cancelled` follows.
    void cancel();

    // ---- the parts that need no process
    // A file name from a title: letters, digits, spaces, "-", "_" and "."; anything else becomes
    // "-"; no leading dot; at most MaxNameLength; "video" when nothing is left.
    [[nodiscard]] static QString safeName(const QString &wished);
    // `<folder>/<base>.mp4`, or `<base>-2.mp4`, `-3`... the first that is not there.
    [[nodiscard]] static QString freePath(const QString &folder, const QString &base);
    // The text of a refusal when the job is not fit to start, empty when it is.
    [[nodiscard]] static QString problemWith(const ConversionJob &job);
    // The FFmpeg arguments that write `temporary` from the job.
    [[nodiscard]] static QStringList arguments(const ConversionJob &job, const QString &encoder, const QString &temporary);
    // Seconds of video done, from a line of `-progress` output ("out_time_us=1234567").
    [[nodiscard]] static std::optional<double> progressSeconds(const QString &line);
    // The encoder to use among those that `ffmpeg -encoders` lists; empty when there is none.
    [[nodiscard]] static QString chooseEncoder(const QString &encodersText);

Q_SIGNALS:
    void progress(double fraction); // 0 to 1
    void finished(const QString &outputFile);
    void failed(const QString &reason);
    void cancelled();

private:
    void readProgress();
    void end(int exitCode, QProcess::ExitStatus status);
    void discardTemporary();

    QString m_program;
    QString m_nice;
    QString m_encoder;
    QProcess *m_process = nullptr;
    QTimer m_timeout;
    ConversionJob m_job;
    QString m_final;
    QString m_temporary;
    QByteArray m_pending;  // the start of a progress line not complete yet
    QByteArray m_errors;   // the end of what FFmpeg said on its error output
    bool m_cancelling = false;
    bool m_timedOut = false;
};

} // namespace bosun
