// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "MediaController.h"
#include "PagesConfig.h"
#include "ScreensaverSplitter.h"

#include <QByteArray>
#include <QObject>
#include <QProcess>
#include <QString>
#include <QTimer>

#include <memory>

namespace galleon {
class StreamDeck;
}

namespace galleond {

// The animated screensaver: after a time without use, a vertical video is
// played over the screen and the 12 keys (see ScreensaverSplitter), and any key,
// dial or (unless allowed) a track starting brings the normal screen back.
//
// The video is decoded by FFmpeg into raw frames on a pipe, so no video
// library is linked and the decoder runs in its own process. FFmpeg is started
// like the thumbnail maker does: no shell, lowest priority, only the `file`
// protocol, no audio or subtitles; the file must be a regular file with a
// known video extension. Frames that arrive faster than they can be sent are
// dropped, newest kept: the picture never lags.
class ScreensaverController : public QObject
{
    Q_OBJECT

public:
    // `deck` and `media` are not owned and must outlive the controller.
    // `media` may be null (then nothing prevents the screensaver).
    ScreensaverController(galleon::StreamDeck *deck, MediaController *media, QObject *parent = nullptr);
    ~ScreensaverController() override;

    // `baseFolder` resolves a relative video path (the pages file's folder).
    // Applies at once: a running screensaver is stopped, the idle time restarts.
    void setConfig(const ScreensaverConfig &config, const QString &baseFolder);

    [[nodiscard]] bool isActive() const { return m_active; }

    // Something was used (a key, a dial): restarts the idle time; wakes the
    // module if the screensaver runs.
    void notifyActivity();

    // For the tests.
    void setFfmpegProgram(const QString &program) { m_program = program; }
    void setIdleTimeoutMs(int ms) { m_idleOverrideMs = ms; restartIdleTimer(); }

Q_SIGNALS:
    // True when the screensaver starts, false when it ends: the other
    // controllers stop drawing and draw again.
    void activeChanged(bool active);

private:
    void onIdle();
    void start();
    void stop();
    void restartIdleTimer();
    void onReadyRead();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void showFrame(const QByteArray &rgb);
    [[nodiscard]] QString resolveVideo(const QString &path) const;
    void wake();

    galleon::StreamDeck *m_deck;
    MediaController *m_media;
    ScreensaverConfig m_config;
    QString m_baseFolder;
    QString m_program = QStringLiteral("ffmpeg");
    std::unique_ptr<ScreensaverSplitter> m_splitter;
    QTimer m_idle;
    int m_idleOverrideMs = 0;
    QProcess *m_process = nullptr;
    QByteArray m_buffer;
    qint64 m_frameBytes = 0;
    bool m_active = false;
    int m_next = 0; // the video shown at the next start: they take turns
    QString m_playing;
    int m_restarts = 0;
    int m_sendFailures = 0;
    bool m_giveUp = false; // started and failed: not again until the next input
};

} // namespace galleond
