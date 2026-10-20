// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "VideoProbe.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStandardPaths>

#include <cmath>

namespace galleond {

namespace {

// "24/1", "30000/1001": a rate as a fraction; 0 when it is not one or is not plausible.
double rateOf(const QString &text)
{
    const QStringList parts = text.split(QLatin1Char('/'));
    if (parts.size() != 2)
        return 0;
    bool numberOk = false;
    bool denominatorOk = false;
    const double number = parts.at(0).toDouble(&numberOk);
    const double denominator = parts.at(1).toDouble(&denominatorOk);
    if (!numberOk || !denominatorOk || denominator <= 0 || number <= 0)
        return 0;
    const double rate = number / denominator;
    return std::isfinite(rate) && rate <= VideoProbe::MaxFps ? rate : 0;
}

} // namespace

VideoProbe::VideoProbe(const QString &ffprobe, QObject *parent)
    : QObject(parent)
{
    if (!QFileInfo(ffprobe).isAbsolute())
        m_program = QStandardPaths::findExecutable(ffprobe);
    else if (QFileInfo(ffprobe).isFile() && QFileInfo(ffprobe).isExecutable()) // not a folder, which is "executable" too
        m_program = ffprobe;
    m_nice = QStandardPaths::findExecutable(QStringLiteral("nice"));
    m_timeout.setSingleShot(true);
    m_timeout.setInterval(TimeoutMs);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (m_process)
            m_process->kill(); // finished() follows and reports no answer
    });
}

VideoProbe::~VideoProbe()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->kill();
        m_process->waitForFinished(1000);
    }
}

VideoInfo VideoProbe::parse(const QByteArray &json)
{
    VideoInfo info;
    if (json.isEmpty() || json.size() > MaxOutputBytes)
        return info;
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (!document.isObject())
        return info;
    const QJsonArray streams = document.object().value(QStringLiteral("streams")).toArray();
    if (streams.isEmpty() || !streams.at(0).isObject())
        return info;
    const QJsonObject stream = streams.at(0).toObject();

    const int width = stream.value(QStringLiteral("width")).toInt(0);
    const int height = stream.value(QStringLiteral("height")).toInt(0);
    if (width < 1 || height < 1 || width > MaxDimension || height > MaxDimension)
        return info;

    double fps = rateOf(stream.value(QStringLiteral("avg_frame_rate")).toString());
    if (fps <= 0)
        fps = rateOf(stream.value(QStringLiteral("r_frame_rate")).toString());

    // The length is a string in `format` (and sometimes in the stream); both are optional.
    double seconds = 0;
    for (const QJsonValue &value : {document.object().value(QStringLiteral("format")).toObject().value(QStringLiteral("duration")),
                                    stream.value(QStringLiteral("duration"))}) {
        bool ok = false;
        const double candidate = value.toString().toDouble(&ok);
        if (ok && std::isfinite(candidate) && candidate > 0 && candidate <= MaxSeconds) {
            seconds = candidate;
            break;
        }
    }

    static const QRegularExpression codecName(QStringLiteral("^[A-Za-z0-9_]{1,32}$"));
    const QString codec = stream.value(QStringLiteral("codec_name")).toString();

    info.valid = true;
    info.width = width;
    info.height = height;
    info.fps = fps;
    info.seconds = seconds;
    info.codec = codecName.match(codec).hasMatch() ? codec : QString();
    return info;
}

void VideoProbe::request(const QString &videoPath)
{
    if (videoPath.isEmpty() || m_queue.contains(videoPath) || videoPath == m_current)
        return;
    if (m_queue.size() >= MaxQueued)
        return;
    m_queue.enqueue(videoPath);
    if (!m_process)
        startNext();
}

void VideoProbe::startNext()
{
    if (m_process || m_queue.isEmpty())
        return;
    const QString video = m_queue.dequeue();
    if (m_program.isEmpty()) {
        Q_EMIT probed(video, VideoInfo{});
        startNext();
        return;
    }
    m_current = video;

    QStringList arguments;
    QString program = m_program;
    if (!m_nice.isEmpty()) {
        arguments << QStringLiteral("-n") << QStringLiteral("19") << m_program;
        program = m_nice;
    }
    arguments << QStringLiteral("-v") << QStringLiteral("error") << QStringLiteral("-protocol_whitelist") << QStringLiteral("file")
              << QStringLiteral("-select_streams") << QStringLiteral("v:0") << QStringLiteral("-show_entries")
              << QStringLiteral("stream=codec_name,width,height,avg_frame_rate,r_frame_rate,duration:format=duration")
              << QStringLiteral("-of") << QStringLiteral("json")
              << QStringLiteral("file:") + QFileInfo(video).absoluteFilePath(); // "file:" so a name never reads as an option

    m_process = new QProcess(this);
    m_process->setProgram(program);
    m_process->setArguments(arguments);
    m_process->setStandardInputFile(QProcess::nullDevice());
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::finished, this, [this, video](int code, QProcess::ExitStatus status) {
        const QByteArray output = m_process->readAllStandardOutput();
        finish(video, output, status == QProcess::NormalExit && code == 0);
    });
    connect(m_process, &QProcess::errorOccurred, this, [this, video](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            finish(video, {}, false);
    });
    m_timeout.start();
    m_process->start();
}

void VideoProbe::finish(const QString &videoPath, const QByteArray &output, bool ok)
{
    m_timeout.stop();
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
    m_current.clear();
    Q_EMIT probed(videoPath, ok ? parse(output) : VideoInfo{});
    startNext();
}

} // namespace galleond
