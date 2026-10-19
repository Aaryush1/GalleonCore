// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScreensaverController.h"

#include "VideoThumbnails.h"

#include <galleon/StreamDeck.h>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>

namespace galleond {

namespace {

constexpr qint64 MaxVideoBytes = 8LL * 1024 * 1024 * 1024;
constexpr int MaxRestarts = 3;      // a video that ends on its own (a failed loop) is restarted this many times
constexpr int MaxSendFailures = 30; // consecutive frames the module did not take
constexpr int MaxBufferedFrames = 4;
constexpr int StopWaitMs = 1500;

} // namespace

ScreensaverController::ScreensaverController(galleon::StreamDeck *deck, MediaController *media, QObject *parent)
    : QObject(parent)
    , m_deck(deck)
    , m_media(media)
{
    m_idle.setSingleShot(true);
    connect(&m_idle, &QTimer::timeout, this, &ScreensaverController::onIdle);

    connect(m_deck, &galleon::StreamDeck::keyPressed, this, [this] { notifyActivity(); });
    connect(m_deck, &galleon::StreamDeck::keyReleased, this, [this] { notifyActivity(); });
    connect(m_deck, &galleon::StreamDeck::dialPressed, this, [this] { notifyActivity(); });
    connect(m_deck, &galleon::StreamDeck::dialReleased, this, [this] { notifyActivity(); });
    connect(m_deck, &galleon::StreamDeck::dialRotated, this, [this] { notifyActivity(); });
    if (m_media) {
        // A track starting is a reason to leave the screensaver, unless it is allowed over one.
        connect(m_media, &MediaController::stateChanged, this, [this] {
            if (m_active && !m_config.whilePlaying && m_media->state() == MediaController::State::Playing)
                wake();
        });
    }
}

ScreensaverController::~ScreensaverController()
{
    stop();
}

void ScreensaverController::setConfig(const ScreensaverConfig &config, const QString &baseFolder)
{
    if (config == m_config && baseFolder == m_baseFolder)
        return;
    const bool wasActive = m_active;
    stop();
    m_config = config;
    m_baseFolder = baseFolder;
    m_giveUp = false;
    m_splitter = config.enabled() ? std::make_unique<ScreensaverSplitter>(config) : nullptr;
    if (config.enabled() && !m_splitter->isValid()) {
        qWarning() << "galleond: the screensaver geometry gives an unusable picture size: the screensaver stays off";
        m_splitter.reset();
    } else if (!config.videos.isEmpty() && !config.hasGeometry) {
        qWarning() << "galleond: the screensaver has a video but no geometry (screensaver.geometry, measured in mm): it stays off";
    }
    if (wasActive)
        Q_EMIT activeChanged(false);
    restartIdleTimer();
}

void ScreensaverController::restartIdleTimer()
{
    m_idle.stop();
    if (!m_splitter)
        return;
    m_idle.start(m_idleOverrideMs > 0 ? m_idleOverrideMs : m_config.idleMinutes * 60 * 1000);
}

void ScreensaverController::notifyActivity()
{
    m_giveUp = false;
    if (m_active) {
        // Deferred, so that the other handlers of this same input (which run
        // after this one) still see the screensaver and ignore it: the press
        // that wakes the module does not also act.
        QMetaObject::invokeMethod(this, [this] { wake(); }, Qt::QueuedConnection);
        return;
    }
    restartIdleTimer();
}

void ScreensaverController::onIdle()
{
    if (m_active || !m_splitter || m_giveUp || !m_deck->isOpen())
        return;
    if (!m_config.whilePlaying && m_media && m_media->state() == MediaController::State::Playing) {
        restartIdleTimer(); // look again later
        return;
    }
    start();
}

QString ScreensaverController::resolveVideo(const QString &configured) const
{
    QString path = configured;
    if (path.startsWith(QLatin1String("~/")))
        path = QDir::homePath() + path.mid(1);
    if (QDir::isRelativePath(path))
        path = QDir(m_baseFolder).absoluteFilePath(path);
    const QFileInfo info(path);
    if (!info.isFile() || info.size() <= 0 || info.size() > MaxVideoBytes || !VideoThumbnails::isVideoFile(path))
        return {};
    return info.absoluteFilePath();
}

void ScreensaverController::start()
{
    // The next readable video of the list; the others are skipped with a warning.
    QString video;
    QString title;
    for (qsizetype tried = 0; tried < m_config.videos.size() && video.isEmpty(); ++tried) {
        const qsizetype index = (m_next + tried) % m_config.videos.size();
        const QString configured = m_config.videos.at(index);
        video = resolveVideo(configured);
        title = index < m_config.titles.size() ? m_config.titles.at(index) : QString();
        if (video.isEmpty())
            qWarning() << "galleond: the screensaver video" << configured << "is not a readable video file (mp4, mkv, webm, avi, mov...)";
        else
            m_next = static_cast<int>((m_next + tried + 1) % m_config.videos.size());
    }
    if (video.isEmpty()) {
        qWarning() << "galleond: no usable screensaver video: the screensaver stays off";
        m_giveUp = true;
        return;
    }
    m_playing = video;
    qInfo().noquote() << "galleond: screensaver video" << (title.isEmpty() ? video : QStringLiteral("\"%1\" (%2)").arg(title, video));
    const QString program = QStandardPaths::findExecutable(m_program);
    if (program.isEmpty() && !QFileInfo(m_program).isExecutable()) {
        qWarning() << "galleond: FFmpeg not found: the screensaver stays off";
        m_giveUp = true;
        return;
    }

    const QSize canvas = m_splitter->canvasSize();
    m_frameBytes = static_cast<qint64>(canvas.width()) * canvas.height() * 3;
    const QString filter = QStringLiteral("scale=%1:%2:force_original_aspect_ratio=increase,crop=%1:%2,fps=%3")
                               .arg(canvas.width())
                               .arg(canvas.height())
                               .arg(m_config.fps);
    QStringList arguments{QStringLiteral("-nostdin"), QStringLiteral("-v"), QStringLiteral("error"),
                          QStringLiteral("-protocol_whitelist"), QStringLiteral("file"),
                          QStringLiteral("-re"), QStringLiteral("-stream_loop"), QStringLiteral("-1"),
                          QStringLiteral("-i"), video,
                          QStringLiteral("-an"), QStringLiteral("-sn"), QStringLiteral("-dn"),
                          QStringLiteral("-vf"), filter,
                          QStringLiteral("-f"), QStringLiteral("rawvideo"), QStringLiteral("-pix_fmt"), QStringLiteral("rgb24"),
                          QStringLiteral("-")};
    QString executable = program.isEmpty() ? m_program : program;
    const QString nice = QStandardPaths::findExecutable(QStringLiteral("nice"));
    if (!nice.isEmpty()) {
        arguments.prepend(executable);
        arguments.prepend(QStringLiteral("19"));
        arguments.prepend(QStringLiteral("-n"));
        executable = nice;
    }

    m_buffer.clear();
    m_sinceShown.invalidate();
    m_lastCostMs = 0;
    m_sendFailures = 0;
    m_process = new QProcess(this);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &ScreensaverController::onReadyRead);
    connect(m_process, &QProcess::finished, this, &ScreensaverController::onFinished);
    // Nothing to say on stderr but errors (-v error): they go to the log.
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        qWarning().noquote() << "ffmpeg:" << QString::fromUtf8(m_process->readAllStandardError()).trimmed().left(300);
    });
    m_process->start(executable, arguments, QIODevice::ReadOnly);
    if (!m_process->waitForStarted(3000)) {
        qWarning() << "galleond: FFmpeg did not start: the screensaver stays off";
        m_process->deleteLater();
        m_process = nullptr;
        m_giveUp = true;
        return;
    }
    m_active = true;
    qInfo() << "galleond: screensaver on";
    Q_EMIT activeChanged(true);
}

void ScreensaverController::stop()
{
    if (!m_process)
        return;
    QProcess *process = m_process;
    m_process = nullptr;
    process->disconnect(this);
    process->terminate();
    if (!process->waitForFinished(StopWaitMs)) {
        process->kill();
        process->waitForFinished(StopWaitMs);
    }
    delete process;
    m_buffer.clear();
}

void ScreensaverController::wake()
{
    if (!m_active)
        return;
    stop();
    m_active = false;
    m_restarts = 0;
    qInfo() << "galleond: screensaver off";
    Q_EMIT activeChanged(false);
    restartIdleTimer();
}

void ScreensaverController::onReadyRead()
{
    if (!m_process || m_frameBytes <= 0)
        return;
    m_buffer.append(m_process->readAllStandardOutput());
    // Only the newest whole frame is shown: older ones are late.
    if (m_buffer.size() < m_frameBytes)
        return;
    const qint64 complete = (m_buffer.size() / m_frameBytes) * m_frameBytes;
    const QByteArray latest = m_buffer.mid(complete - m_frameBytes, m_frameBytes);
    m_buffer.remove(0, complete);
    if (m_buffer.size() > MaxBufferedFrames * m_frameBytes)
        m_buffer.clear(); // cannot happen with the line above; a bound all the same
    // Cutting and sending a frame takes this thread for a while, and the dials and
    // keys are read by it too: never use more than half of the time. A frame that
    // arrives sooner than the last one took to send is dropped.
    if (m_sinceShown.isValid() && m_sinceShown.elapsed() < m_lastCostMs)
        return;
    QElapsedTimer cost;
    cost.start();
    showFrame(latest);
    m_lastCostMs = cost.elapsed();
    m_sinceShown.start();
}

void ScreensaverController::showFrame(const QByteArray &rgb)
{
    if (!m_active || !m_splitter)
        return;
    const QSize canvas = m_splitter->canvasSize();
    const QImage frame = ScreensaverSplitter::imageFromRgb888(rgb, canvas);
    const ScreensaverSplitter::Frames parts = m_splitter->split(frame);
    if (!parts.isValid())
        return;
    // A stream of pictures: each one is shown when the next arrives, so none is sent twice.
    bool ok = m_deck->setScreenImage(parts.screen, 85, false);
    for (int key = 0; key < galleon::protocol::KeyCount; ++key)
        ok = m_deck->setKeyImage(key, parts.keys.at(static_cast<std::size_t>(key)), false) && ok;
    if (ok) {
        m_sendFailures = 0;
    } else if (++m_sendFailures >= MaxSendFailures) {
        qWarning() << "galleond: the module does not take the screensaver pictures: back to the normal screen";
        m_giveUp = true;
        wake();
    }
}

void ScreensaverController::onFinished(int exitCode, QProcess::ExitStatus status)
{
    if (!m_process)
        return;
    qWarning() << "galleond: FFmpeg stopped (" << exitCode << (status == QProcess::CrashExit ? "crash" : "") << ")";
    QProcess *process = m_process;
    m_process = nullptr;
    process->disconnect(this);
    process->deleteLater();
    // With -stream_loop the video does not end: a stop is a failure. Try a few
    // times, then give up until the next use.
    if (m_active && ++m_restarts <= MaxRestarts && !m_playing.isEmpty()) {
        m_active = false; // start() sets it and announces it again
        start();
        if (m_active)
            return;
    }
    const bool wasActive = m_active;
    m_active = false;
    m_giveUp = true;
    if (wasActive || m_restarts > 0)
        Q_EMIT activeChanged(false);
    restartIdleTimer();
}

} // namespace galleond
