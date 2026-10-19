// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "VideoThumbnails.h"

#include "CoverLoader.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <algorithm>

namespace galleond {

bool VideoThumbnails::isVideoFile(const QString &path)
{
    static const QStringList extensions{QStringLiteral("mp4"), QStringLiteral("mkv"), QStringLiteral("webm"),
                                        QStringLiteral("avi"), QStringLiteral("mov"), QStringLiteral("m4v"),
                                        QStringLiteral("mpg"), QStringLiteral("mpeg"), QStringLiteral("wmv"),
                                        QStringLiteral("flv"), QStringLiteral("ogv")};
    return extensions.contains(QFileInfo(path).suffix().toLower());
}

VideoThumbnails::VideoThumbnails(QString cacheFolder, const QString& ffmpeg, QObject *parent)
    : QObject(parent)
    , m_folder(std::move(cacheFolder))
{
    m_program = QFileInfo(ffmpeg).isAbsolute() ? (QFileInfo(ffmpeg).isExecutable() ? ffmpeg : QString())
                                                : QStandardPaths::findExecutable(ffmpeg);
    m_nice = QStandardPaths::findExecutable(QStringLiteral("nice"));
    if (m_program.isEmpty()) {
        qInfo() << "galleond: ffmpeg not found: no thumbnails for videos";
        return;
    }
    QDir().mkpath(m_folder);
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(TimeoutMs);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (m_process) {
            qWarning() << "galleond: ffmpeg took too long, stopped";
            m_process->kill();
        }
    });
    prune();
}

VideoThumbnails::~VideoThumbnails()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    if (!m_temporary.isEmpty())
        QFile::remove(m_temporary);
}

QString VideoThumbnails::keyFile(const QString &videoPath) const
{
    const QFileInfo info(videoPath);
    // Path, size and date: a file replaced under the same name is a new video.
    const QByteArray identity = info.absoluteFilePath().toUtf8() + '|' + QByteArray::number(info.size()) + '|'
                                + QByteArray::number(info.lastModified().toMSecsSinceEpoch());
    return QDir(m_folder).filePath(QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex())
                                   + QStringLiteral(".jpg"));
}

QString VideoThumbnails::cached(const QString &videoPath) const
{
    if (!isAvailable() || !isVideoFile(videoPath))
        return {};
    const QFileInfo info(videoPath);
    if (!info.isFile())
        return {};
    const QString file = keyFile(videoPath);
    return QFileInfo::exists(file) ? file : QString();
}

void VideoThumbnails::request(const QString &videoPath, double durationSeconds)
{
    if (!isAvailable() || !isVideoFile(videoPath) || !QFileInfo(videoPath).isFile())
        return;
    if (QFileInfo::exists(keyFile(videoPath))) {
        Q_EMIT thumbnailReady(videoPath, keyFile(videoPath));
        return;
    }
    const Job job{.video=videoPath, .duration=durationSeconds};
    if (m_busy) {
        m_pending = job;
        m_hasPending = true;
        return;
    }
    const double at = durationSeconds > 20 ? std::clamp(durationSeconds * 0.1, 1.0, 60.0) : 1.0;
    start(job, at);
}

void VideoThumbnails::start(const Job &job, double at)
{
    m_busy = true;
    m_temporary = keyFile(job.video) + QStringLiteral(".tmp.jpg");
    QFile::remove(m_temporary);

    QStringList arguments;
    QString program = m_program;
    if (!m_nice.isEmpty()) { // the lowest priority: a film must not slow the desktop down
        arguments << QStringLiteral("-n") << QStringLiteral("19") << m_program;
        program = m_nice;
    }
    arguments << QStringLiteral("-nostdin") << QStringLiteral("-hide_banner") << QStringLiteral("-loglevel")
              << QStringLiteral("error") << QStringLiteral("-y")
              << QStringLiteral("-protocol_whitelist") << QStringLiteral("file") // no network, whatever the file says
              << QStringLiteral("-ss") << QString::number(at, 'f', 1) << QStringLiteral("-i")
              << QStringLiteral("file:") + QFileInfo(job.video).absoluteFilePath() // "file:" so a name never reads as an option
              << QStringLiteral("-frames:v") << QStringLiteral("1") << QStringLiteral("-vf")
              << QStringLiteral("scale='min(%1,iw)':'min(%1,ih)':force_original_aspect_ratio=decrease").arg(Side)
              << QStringLiteral("-q:v") << QStringLiteral("3") << QStringLiteral("-f") << QStringLiteral("image2")
              << m_temporary;

    m_process = new QProcess(this);
    m_process->setProgram(program);
    m_process->setArguments(arguments);
    m_process->setStandardInputFile(QProcess::nullDevice());
    m_process->setStandardOutputFile(QProcess::nullDevice());
    m_process->setStandardErrorFile(QProcess::nullDevice());
    connect(m_process, &QProcess::finished, this, [this, job, at](int code, QProcess::ExitStatus status) {
        finished(job, at, code, status);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this, job, at](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finished(job, at, -1, QProcess::CrashExit);
    });
    m_timeout.start();
    m_process->start();
}

void VideoThumbnails::finished(const Job &job, double at, int exitCode, QProcess::ExitStatus status)
{
    m_timeout.stop();
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }

    // The output counts only if FFmpeg succeeded and it is a sane JPEG.
    QString result;
    if (status == QProcess::NormalExit && exitCode == 0) {
        QFile file(m_temporary);
        if (file.open(QIODevice::ReadOnly)) {
            const QByteArray bytes = file.read(CoverLoader::MaxBytes + 1);
            file.close();
            if (!CoverLoader::decode(bytes).isNull() && QFile::rename(m_temporary, keyFile(job.video)))
                result = keyFile(job.video);
        }
    }
    QFile::remove(m_temporary);
    m_temporary.clear();

    if (result.isEmpty() && at > 0.5) {
        // A moment past the end, or a video that starts late: try the first frame.
        start(job, 0.0);
        return;
    }
    m_busy = false;
    if (!result.isEmpty()) {
        prune();
        Q_EMIT thumbnailReady(job.video, result);
    } else {
        qInfo() << "galleond: no thumbnail made for" << QFileInfo(job.video).fileName();
    }
    next();
}

void VideoThumbnails::next()
{
    if (!m_hasPending || m_busy)
        return;
    m_hasPending = false;
    request(m_pending.video, m_pending.duration);
}

void VideoThumbnails::prune() const
{
    // Oldest first: drop files until the count and the size are inside the limits.
    QFileInfoList files = QDir(m_folder).entryInfoList({QStringLiteral("*.jpg")}, QDir::Files, QDir::NoSort);
    std::ranges::sort(files, [](const QFileInfo &a, const QFileInfo &b) { return a.lastModified() < b.lastModified(); });
    qint64 total = 0;
    for (const QFileInfo &file : std::as_const(files))
        total += file.size();
    qsizetype count = files.size();
    for (const QFileInfo &file : std::as_const(files)) {
        if (count <= MaxEntries && total <= MaxTotalBytes)
            break;
        if (QFile::remove(file.absoluteFilePath())) {
            total -= file.size();
            --count;
        }
    }
}

} // namespace galleond
