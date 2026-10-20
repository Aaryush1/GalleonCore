// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "VideoConverter.h"

#include "FileNames.h"

#include <VideoThumbnails.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStandardPaths>

#include <algorithm>
#include <cmath>

namespace bosun {

namespace {

constexpr int MaxErrorBytes = 2000;
constexpr int EncoderQueryMs = 5000;

QString words(const char *text)
{
    return QCoreApplication::translate("VideoConverter", text);
}

} // namespace

VideoConverter::VideoConverter(const QString &ffmpeg, QObject *parent)
    : QObject(parent)
{
    if (!QFileInfo(ffmpeg).isAbsolute())
        m_program = QStandardPaths::findExecutable(ffmpeg);
    else if (QFileInfo(ffmpeg).isFile() && QFileInfo(ffmpeg).isExecutable())
        m_program = ffmpeg;
    m_nice = QStandardPaths::findExecutable(QStringLiteral("nice"));
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(TimeoutMs);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        m_timedOut = true;
        if (m_process)
            m_process->kill();
    });
    if (m_program.isEmpty())
        return;
    QProcess query;
    query.setProgram(m_program);
    query.setArguments({QStringLiteral("-hide_banner"), QStringLiteral("-encoders")});
    query.setStandardInputFile(QProcess::nullDevice());
    query.start();
    if (query.waitForFinished(EncoderQueryMs) && query.exitStatus() == QProcess::NormalExit)
        m_encoder = chooseEncoder(QString::fromUtf8(query.readAllStandardOutput()));
}

VideoConverter::~VideoConverter()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
    discardTemporary();
}

QString VideoConverter::chooseEncoder(const QString &encodersText)
{
    // A line is "V....D libx264   libx264 H.264 / AVC ...": the type flags, then the name.
    for (const QString &wanted : {QStringLiteral("libx264"), QStringLiteral("mpeg4")}) {
        const QRegularExpression line(QStringLiteral(R"((?m)^\s*V\S*\s+%1\s)").arg(wanted));
        if (line.match(encodersText).hasMatch())
            return wanted;
    }
    return {};
}

QString VideoConverter::safeName(const QString &wished)
{
    return filenames::safeName(wished, QStringLiteral("video"));
}

QString VideoConverter::freePath(const QString &folder, const QString &base)
{
    return filenames::freePath(folder, base, QStringLiteral(".mp4"));
}

QString VideoConverter::problemWith(const ConversionJob &job)
{
    const QFileInfo input(job.input);
    if (!input.isFile() || input.size() <= 0 || !galleond::VideoThumbnails::isVideoFile(job.input))
        return words("The file is not a video that can be converted.");
    if (job.canvas.isEmpty() || job.source.isEmpty())
        return words("The size of the video or of the picture of the module is not known.");
    if (!QRect(QPoint(0, 0), job.source).contains(job.crop) || job.crop.width() < 2 || job.crop.height() < 2)
        return words("The part to keep is not inside the video.");
    if (job.fps < 1 || job.fps > 20)
        return words("The rate must be from 1 to 20 images per second.");
    if (job.outputFolder.isEmpty())
        return words("No folder was given for the result.");
    return {};
}

QStringList VideoConverter::arguments(const ConversionJob &job, const QString &encoder, const QString &temporary)
{
    const QString filter = QStringLiteral("crop=%1:%2:%3:%4,scale=%5:%6:flags=lanczos,fps=%7,format=yuv420p")
                               .arg(job.crop.width()).arg(job.crop.height()).arg(job.crop.x()).arg(job.crop.y())
                               .arg(job.canvas.width()).arg(job.canvas.height()).arg(job.fps);
    QStringList list{QStringLiteral("-nostdin"), QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
                     QStringLiteral("-y"), QStringLiteral("-protocol_whitelist"), QStringLiteral("file"), // no network, whatever the file says
                     QStringLiteral("-i"), QStringLiteral("file:") + QFileInfo(job.input).absoluteFilePath(), // so a name never reads as an option
                     QStringLiteral("-t"), QString::number(MaxSeconds, 'f', 0),
                     QStringLiteral("-vf"), filter, QStringLiteral("-an"), QStringLiteral("-sn"), QStringLiteral("-dn")};
    if (encoder == QLatin1String("libx264"))
        list << QStringLiteral("-c:v") << QStringLiteral("libx264") << QStringLiteral("-preset") << QStringLiteral("medium")
             << QStringLiteral("-crf") << QStringLiteral("20") << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    else
        list << QStringLiteral("-c:v") << QStringLiteral("mpeg4") << QStringLiteral("-q:v") << QStringLiteral("3");
    list << QStringLiteral("-progress") << QStringLiteral("pipe:1") << QStringLiteral("-nostats") << QStringLiteral("-f")
         << QStringLiteral("mp4") << temporary;
    return list;
}

std::optional<double> VideoConverter::progressSeconds(const QString &line)
{
    // FFmpeg calls it out_time_ms, but both hold microseconds.
    static const QRegularExpression time(QStringLiteral(R"(^out_time_(?:us|ms)=(\d{1,15})$)"));
    const QRegularExpressionMatch match = time.match(line.trimmed());
    if (!match.hasMatch())
        return std::nullopt;
    return match.captured(1).toDouble() / 1'000'000.0;
}

QString VideoConverter::start(const ConversionJob &job)
{
    if (m_process)
        return words("A conversion is already running.");
    if (m_encoder.isEmpty())
        return words("FFmpeg was not found, or it has no H.264 or MPEG-4 encoder.");
    if (const QString problem = problemWith(job); !problem.isEmpty())
        return problem;
    if (!QDir().mkpath(job.outputFolder))
        return words("The folder for the result cannot be created.");

    m_job = job;
    m_final = freePath(job.outputFolder, safeName(job.name));
    m_temporary = m_final + QStringLiteral(".partial");
    m_pending.clear();
    m_errors.clear();
    m_cancelling = false;
    m_timedOut = false;

    QStringList list = arguments(job, m_encoder, m_temporary);
    QString program = m_program;
    if (!m_nice.isEmpty()) { // a conversion must not slow the desktop down
        list = QStringList{QStringLiteral("-n"), QStringLiteral("19"), m_program} + list;
        program = m_nice;
    }
    m_process = new QProcess(this);
    m_process->setProgram(program);
    m_process->setArguments(list);
    m_process->setStandardInputFile(QProcess::nullDevice());
    connect(m_process, &QProcess::readyReadStandardOutput, this, &VideoConverter::readProgress);
    connect(m_process, &QProcess::readyReadStandardError, this, [this] {
        m_errors += m_process->readAllStandardError();
        m_errors = m_errors.right(MaxErrorBytes);
    });
    connect(m_process, &QProcess::finished, this, &VideoConverter::end);
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            end(-1, QProcess::CrashExit);
    });
    m_timeout.start();
    m_process->start();
    return {};
}

void VideoConverter::readProgress()
{
    m_pending += m_process->readAllStandardOutput();
    const double total = std::min(m_job.seconds > 0 ? m_job.seconds : MaxSeconds, MaxSeconds);
    qsizetype newline = 0;
    while ((newline = m_pending.indexOf('\n')) >= 0) {
        const QString line = QString::fromUtf8(m_pending.left(newline));
        m_pending.remove(0, newline + 1);
        if (const auto seconds = progressSeconds(line))
            Q_EMIT progress(std::clamp(*seconds / total, 0.0, 1.0));
    }
    m_pending = m_pending.right(MaxErrorBytes); // a line that never ends is not kept
}

void VideoConverter::discardTemporary()
{
    if (!m_temporary.isEmpty())
        QFile::remove(m_temporary);
    m_temporary.clear();
}

void VideoConverter::end(int exitCode, QProcess::ExitStatus status)
{
    m_timeout.stop();
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    if (m_cancelling) {
        discardTemporary();
        Q_EMIT cancelled();
        return;
    }
    const bool wroteSomething = QFileInfo(m_temporary).size() > 0;
    if (status != QProcess::NormalExit || exitCode != 0 || !wroteSomething) {
        discardTemporary();
        const QString said = QString::fromUtf8(m_errors).trimmed().right(300);
        QString reason = said.isEmpty() ? words("FFmpeg could not convert this video.") : said;
        if (m_timedOut)
            reason = words("The conversion took too long and was stopped.");
        Q_EMIT failed(reason);
        return;
    }
    // Never over a file that appeared meanwhile.
    const QString target = QFileInfo::exists(m_final) ? freePath(m_job.outputFolder, safeName(m_job.name)) : m_final;
    if (!QFile::rename(m_temporary, target)) {
        discardTemporary();
        Q_EMIT failed(words("The result could not be saved in its folder."));
        return;
    }
    m_temporary.clear();
    Q_EMIT progress(1.0);
    Q_EMIT finished(target);
}

void VideoConverter::cancel()
{
    if (!m_process)
        return;
    m_cancelling = true;
    m_process->kill();
}

} // namespace bosun
