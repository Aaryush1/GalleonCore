// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <InitialPages.h>

#include "BosunBackend.h"

#include "CropMath.h"
#include "PageName.h"
#include "VideoAdvice.h"

#include <DesktopEntries.h>
#include <KeyInjector.h>
#include <ScreensaverController.h>
#include <ScreensaverSplitter.h>
#include <SteamLibrary.h>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>
#include <QVariantMap>

#include <array>
#include <cmath>

namespace bosun {

namespace {

using galleond::ActionType;
using galleond::MediaCommand;

constexpr int KeyCount = galleon::protocol::KeyCount;

// The names the pages file uses for the media commands, with the words the window shows.
struct MediaEntry
{
    MediaCommand command;
    const char *id;
    const char *words;
};

constexpr std::array<MediaEntry, 12> MediaEntries{{
    {.command = MediaCommand::PlayPause, .id = "play-pause", .words = "Play / pause"},
    {.command = MediaCommand::Next, .id = "next", .words = "Next track"},
    {.command = MediaCommand::Previous, .id = "previous", .words = "Previous track"},
    {.command = MediaCommand::Stop, .id = "stop", .words = "Stop"},
    {.command = MediaCommand::Shuffle, .id = "shuffle", .words = "Shuffle"},
    {.command = MediaCommand::Repeat, .id = "repeat", .words = "Repeat"},
    {.command = MediaCommand::Mute, .id = "mute", .words = "Mute the sound"},
    {.command = MediaCommand::Raise, .id = "raise", .words = "Bring the player to the front"},
    {.command = MediaCommand::Fullscreen, .id = "fullscreen", .words = "Full screen"},
    {.command = MediaCommand::Restart, .id = "restart", .words = "Restart the track"},
    {.command = MediaCommand::Volume, .id = "volume", .words = "Volume"},
    {.command = MediaCommand::Seek, .id = "seek", .words = "Move in the track"},
}};

QString mediaId(MediaCommand command)
{
    for (const MediaEntry &entry : MediaEntries) {
        if (entry.command == command)
            return QLatin1String(entry.id);
    }
    return {};
}

QString mediaName(MediaCommand command)
{
    switch (command) {
    case MediaCommand::PlayPause: return QStringLiteral("play / pause");
    case MediaCommand::Next: return QStringLiteral("next track");
    case MediaCommand::Previous: return QStringLiteral("previous track");
    case MediaCommand::Stop: return QStringLiteral("stop");
    case MediaCommand::Volume: return QStringLiteral("volume");
    case MediaCommand::Seek: return QStringLiteral("seek");
    case MediaCommand::Shuffle: return QStringLiteral("shuffle");
    case MediaCommand::Repeat: return QStringLiteral("repeat");
    case MediaCommand::Mute: return QStringLiteral("mute");
    case MediaCommand::Raise: return QStringLiteral("raise the player");
    case MediaCommand::Fullscreen: return QStringLiteral("full screen");
    case MediaCommand::Restart: return QStringLiteral("restart the track");
    }
    return {};
}

struct Described
{
    QString kind;
    QString detail;
};

// What the key does, in words, for the detail panel.
Described describe(const galleond::KeyConfig &key)
{
    const galleond::Action &action = key.action;
    switch (action.type) {
    case ActionType::App: return {.kind = QStringLiteral("app"), .detail = action.desktop};
    case ActionType::Media: return {.kind = QStringLiteral("media"), .detail = mediaName(action.command)};
    case ActionType::Page: return {.kind = QStringLiteral("page"), .detail = action.page};
    case ActionType::Back: return {.kind = QStringLiteral("back"), .detail = {}};
    case ActionType::Steam: return {.kind = QStringLiteral("steam"), .detail = QString::number(action.appid)};
    case ActionType::Key: return {.kind = QStringLiteral("key"), .detail = action.keyName};
    case ActionType::Theme: return {.kind = QStringLiteral("theme"), .detail = {}};
    case ActionType::WallpaperMode: return {.kind = QStringLiteral("wallpaper-mode"), .detail = {}};
    case ActionType::None: return {.kind = QStringLiteral("none"), .detail = {}};
    }
    return {};
}

// What a key is set to, for the editor: the fields of its action as the file has them.
QVariantMap configuredFields(const galleond::KeyConfig &key)
{
    const galleond::Action &action = key.action;
    return QVariantMap{{QStringLiteral("type"), describe(key).kind},
            {QStringLiteral("command"), action.type == ActionType::Media ? mediaId(action.command) : QString()},
            {QStringLiteral("step"), action.step},
            {QStringLiteral("seconds"), action.seconds},
            {QStringLiteral("pageTarget"), action.page},
            {QStringLiteral("desktop"), action.desktop},
            {QStringLiteral("keyName"), action.keyName},
            {QStringLiteral("appid"), action.appid},
            {QStringLiteral("rawLabel"), key.label},
            {QStringLiteral("rawIcon"), key.icon.name}};
}


} // namespace

QString BosunBackend::defaultConvertedFolder()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/galleoncore/screensavers");
}

BosunBackend::BosunBackend(QString pagesFile, QObject *parent)
    : BosunBackend(std::move(pagesFile), Tools{}, parent)
{
}

BosunBackend::BosunBackend(QString pagesFile, const Tools &tools, QObject *parent)
    : QObject(parent)
    , m_file(std::move(pagesFile))
    , m_tiles(QFileInfo(m_file).absolutePath())
    , m_probe(tools.ffprobe)
    , m_thumbnails(tools.thumbnailFolder.isEmpty()
                       ? QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/galleoncore/thumbnails")
                       : tools.thumbnailFolder,
                   tools.ffmpeg)
    , m_converter(tools.ffmpeg)
    , m_convertedFolder(tools.convertedFolder)
{
    qRegisterMetaType<galleond::VideoInfo>();
    connect(&m_converter, &VideoConverter::progress, this, [this](double fraction) { setConversion(QStringLiteral("running"), fraction); });
    connect(&m_converter, &VideoConverter::finished, this, &BosunBackend::conversionDone);
    connect(&m_converter, &VideoConverter::failed, this, [this](const QString &reason) { setConversion(QStringLiteral("failed"), 0, reason); });
    connect(&m_converter, &VideoConverter::cancelled, this, [this] { setConversion(QStringLiteral("cancelled")); });
    connect(&m_probe, &galleond::VideoProbe::probed, this, [this](const QString &path, const galleond::VideoInfo &info) {
        m_videoInfo.insert(path, info);
        buildScreensaver();
        buildCandidate();
        Q_EMIT changed();
        nextThumbnail(); // the length picks the moment of the picture
    });
    connect(&m_thumbnails, &galleond::VideoThumbnails::thumbnailReady, this, [this](const QString &path, const QString &picture) {
        m_videoPictures.insert(path, picture);
        m_pictureBusy = false;
        buildScreensaver();
        buildCandidate();
        Q_EMIT changed();
        nextThumbnail();
    });
    connect(&m_thumbnails, &galleond::VideoThumbnails::thumbnailFailed, this, [this] {
        m_pictureBusy = false;
        nextThumbnail();
    });
    m_reloadSoon.setSingleShot(true);
    m_reloadSoon.setInterval(200);
    connect(&m_reloadSoon, &QTimer::timeout, this, &BosunBackend::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_reloadSoon, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_reloadSoon, qOverload<>(&QTimer::start));
    reload();
}

void BosunBackend::watchFile()
{
    // A file replaced by an editor drops out of the watch: watch again each time.
    const QString folder = QFileInfo(m_file).absolutePath();
    if (!m_watcher.directories().contains(folder))
        m_watcher.addPath(folder);
    if (QFileInfo::exists(m_file) && !m_watcher.files().contains(m_file))
        m_watcher.addPath(m_file);
}

void BosunBackend::reload()
{
    watchFile();
    ++m_revision;
    m_themes.clear();
    m_config.reset();
    m_problem.clear();
    m_screensaver.clear();
    m_videoFiles.clear();

    QByteArray json;
    if (QFileInfo::exists(m_file)) {
        QFile file(m_file);
        if (!file.open(QIODevice::ReadOnly)) {
            m_problem = tr("Cannot read %1").arg(m_file);
            Q_EMIT changed();
            return;
        }
        json = file.read(galleond::MaxPagesFileBytes + 1);
    }
    else {
        json = galleond::initialPagesJson();
    }
    json = galleond::preparePagesJson(json);
    const galleond::PagesParseResult parsed = galleond::parsePagesConfig(json);
    if (!parsed.config) {
        m_problem = tr("The pages file has errors:\n%1").arg(parsed.errors.join(QLatin1Char('\n')));
        Q_EMIT changed();
        return;
    }
    m_config = parsed.config;
    buildScreensaver();
    buildCandidate(); // the list changed: it may be full now, or hold the video
    askAboutVideos();
    for (const galleond::Theme &theme : parsed.config->themes) {
        m_themes.append(QVariantMap{{QStringLiteral("name"), theme.name},
                                    {QStringLiteral("page"), theme.page},
                                    {QStringLiteral("color"), QColor::fromRgb(theme.rgb).name()}});
    }
    Q_EMIT changed();
}

void BosunBackend::buildScreensaver()
{
    m_screensaver.clear();
    if (!m_config)
        return;
    const galleond::ScreensaverConfig &config = m_config->screensaver;
    const galleond::ScreensaverSplitter splitter(config);
    const QSize canvas = splitter.isValid() ? splitter.canvasSize() : QSize();

    // The video list is resolved once per reading of the file (askAboutVideos); the rest is words.
    if (m_videoFiles.size() != config.videos.size()) {
        m_videoFiles.clear();
        const QString base = QFileInfo(m_file).absolutePath();
        for (const QString &configured : config.videos)
            m_videoFiles.append(galleond::ScreensaverController::resolveVideoPath(configured, base));
    }

    QVariantList videos;
    for (qsizetype index = 0; index < config.videos.size(); ++index) {
        const QString &configured = config.videos.at(index);
        const QString &path = m_videoFiles.at(index);
        QStringList notes;
        QString details;
        if (path.isEmpty()) {
            notes << tr("The file is missing, or is not a video that galleond can read.");
        } else if (m_videoInfo.contains(path)) {
            const VideoAdvice advice = adviseOn(m_videoInfo.value(path), canvas, config.fps);
            details = advice.details;
            notes = advice.notes;
        }
        const QString picture = m_videoPictures.value(path);
        videos.append(QVariantMap{{QStringLiteral("index"), static_cast<int>(index)},
                                  {QStringLiteral("title"), index < config.titles.size() ? config.titles.at(index) : QString()},
                                  {QStringLiteral("file"), configured},
                                  {QStringLiteral("name"), QFileInfo(configured).fileName()},
                                  {QStringLiteral("found"), !path.isEmpty()},
                                  {QStringLiteral("details"), details},
                                  {QStringLiteral("thumbnail"), picture.isEmpty() ? QString() : QUrl::fromLocalFile(picture).toString()},
                                  {QStringLiteral("notes"), notes}});
    }
    m_screensaver = QVariantMap{{QStringLiteral("enabled"), config.enabled()},
                                {QStringLiteral("hasGeometry"), config.hasGeometry},
                                {QStringLiteral("idleMinutes"), config.idleMinutes},
                                {QStringLiteral("fps"), config.fps},
                                {QStringLiteral("whilePlaying"), config.whilePlaying},
                                {QStringLiteral("probeMissing"), !m_probe.isAvailable()},
                                {QStringLiteral("canvas"), canvas.isEmpty() ? QString() : QStringLiteral("%1 × %2").arg(canvas.width()).arg(canvas.height())},
                                {QStringLiteral("videos"), videos}};
}

void BosunBackend::askAboutVideos()
{
    m_pictureTried.clear();
    for (const QString &path : std::as_const(m_videoFiles)) {
        if (m_probe.isAvailable() && !path.isEmpty() && !m_videoInfo.contains(path))
            m_probe.request(path);
    }
    nextThumbnail(); // those whose length is known already; the others follow their answer
}

void BosunBackend::nextThumbnail()
{
    if (!m_thumbnails.isAvailable() || m_pictureBusy)
        return;
    QStringList wanted = m_videoFiles;
    wanted << m_candidatePath;
    for (const QString &path : std::as_const(wanted)) {
        if (path.isEmpty() || m_videoPictures.contains(path) || m_pictureTried.contains(path))
            continue;
        // Wait for the length of the first ones so that the picture is taken a tenth of the way in.
        if (m_probe.isAvailable() && !m_videoInfo.contains(path))
            return;
        m_pictureTried.append(path);
        m_pictureBusy = true;
        m_thumbnails.request(path, m_videoInfo.value(path).seconds);
        return;
    }
}

QString BosunBackend::applied(const galleond::EditResult &result)
{
    if (!result.ok)
        return result.errors.join(QLatin1Char('\n'));
    reload();
    return {};
}

QString BosunBackend::setIdleMinutes(int minutes)
{
    return applied(galleond::PagesEditor::setScreensaverIdleMinutes(m_file, minutes));
}

QString BosunBackend::setFps(int fps)
{
    return applied(galleond::PagesEditor::setScreensaverFps(m_file, fps));
}

QString BosunBackend::setWhilePlaying(bool whilePlaying)
{
    return applied(galleond::PagesEditor::setScreensaverWhilePlaying(m_file, whilePlaying));
}

QString BosunBackend::setVideoTitle(int index, const QString &title)
{
    return applied(galleond::PagesEditor::setScreensaverVideoTitle(m_file, index, title));
}

QString BosunBackend::removeVideo(int index)
{
    return applied(galleond::PagesEditor::removeScreensaverVideo(m_file, index));
}

void BosunBackend::inspect(const QString &pathOrUrl)
{
    const QString chosen = pathOrUrl.trimmed();
    const QUrl url(chosen);
    const QString path = url.isLocalFile() ? url.toLocalFile() : chosen;
    m_candidateChosen = chosen;
    m_candidatePath = galleond::ScreensaverController::resolveVideoPath(path, QFileInfo(m_file).absolutePath());
    m_candidateProblem = m_candidatePath.isEmpty() ? tr("This is not a video file that galleond can read.") : QString();
    if (!m_candidatePath.isEmpty()) {
        m_pictureTried.removeAll(m_candidatePath);
        if (m_probe.isAvailable() && !m_videoInfo.contains(m_candidatePath))
            m_probe.request(m_candidatePath);
        nextThumbnail();
    }
    buildCandidate();
    Q_EMIT changed();
}

void BosunBackend::clearCandidate()
{
    m_candidatePath.clear();
    m_candidateProblem.clear();
    m_candidateChosen.clear();
    buildCandidate();
    Q_EMIT changed();
}

QString BosunBackend::addCandidate(const QString &title)
{
    if (m_candidatePath.isEmpty())
        return tr("No video was chosen.");
    if (!m_candidate.value(QStringLiteral("canAdd")).toBool())
        return m_candidate.value(QStringLiteral("why")).toString();
    const QString suggested = m_candidate.value(QStringLiteral("suggestedTitle")).toString();
    const QString error = applied(galleond::PagesEditor::addScreensaverVideo(m_file, m_candidatePath, title.trimmed().isEmpty() ? suggested : title));
    if (error.isEmpty())
        clearCandidate();
    return error;
}

void BosunBackend::setConversion(const QString &state, double fraction, const QString &message)
{
    m_conversion = QVariantMap{{QStringLiteral("state"), state}, {QStringLiteral("fraction"), fraction}, {QStringLiteral("message"), message}};
    Q_EMIT changed();
}

QString BosunBackend::startConversion(int x, int y, int width, int height, const QString &title)
{
    if (m_candidatePath.isEmpty() || !m_candidate.value(QStringLiteral("canCut")).toBool()) {
        for (const QString &field : {QStringLiteral("cutWhy"), QStringLiteral("why")}) {
            if (const QString why = m_candidate.value(field).toString(); !why.isEmpty())
                return why;
        }
        return tr("No video can be converted now.");
    }
    const galleond::VideoInfo info = m_videoInfo.value(m_candidatePath);
    const galleond::ScreensaverConfig saver = m_config ? m_config->screensaver : galleond::ScreensaverConfig{};
    const QSize canvas = galleond::ScreensaverSplitter(saver).canvasSize();
    if (!info.valid || canvas.isEmpty())
        return tr("The size of the video, or the measures of the module, are not known: nothing to cut it to.");

    ConversionJob job;
    job.input = m_candidatePath;
    job.source = QSize(info.width, info.height);
    job.crop = crop::fitted(QRect(x, y, width, height), job.source, canvas);
    job.canvas = canvas;
    job.fps = saver.fps;
    job.seconds = info.seconds;
    job.outputFolder = m_convertedFolder;
    m_conversionTitle = title.trimmed().isEmpty() ? m_candidate.value(QStringLiteral("suggestedTitle")).toString() : title.trimmed();
    job.name = m_conversionTitle;
    if (const QString refused = m_converter.start(job); !refused.isEmpty())
        return refused;
    setConversion(QStringLiteral("running"), 0);
    return {};
}

void BosunBackend::cancelConversion()
{
    m_converter.cancel();
}

void BosunBackend::conversionDone(const QString &outputFile)
{
    const QString error = applied(galleond::PagesEditor::addScreensaverVideo(m_file, outputFile, m_conversionTitle));
    if (!error.isEmpty()) {
        setConversion(QStringLiteral("failed"), 1, tr("The video was converted (%1) but could not be added: %2").arg(outputFile, error));
        return;
    }
    m_candidatePath.clear();
    m_candidateChosen.clear();
    m_candidateProblem.clear();
    buildCandidate();
    setConversion(QStringLiteral("done"), 1, outputFile);
}

QSize BosunBackend::canvas() const
{
    const galleond::ScreensaverConfig saver = m_config ? m_config->screensaver : galleond::ScreensaverConfig{};
    return galleond::ScreensaverSplitter(saver).canvasSize();
}

QSize BosunBackend::candidateSource() const
{
    const galleond::VideoInfo info = m_videoInfo.value(m_candidatePath);
    return info.valid ? QSize(info.width, info.height) : QSize();
}

namespace {

QVariantMap rectMap(const QRect &rect)
{
    return QVariantMap{{QStringLiteral("x"), rect.x()}, {QStringLiteral("y"), rect.y()},
                       {QStringLiteral("width"), rect.width()}, {QStringLiteral("height"), rect.height()}};
}

QRect rectOf(const QVariantMap &map)
{
    return {map.value(QStringLiteral("x")).toInt(), map.value(QStringLiteral("y")).toInt(), map.value(QStringLiteral("width")).toInt(),
            map.value(QStringLiteral("height")).toInt()};
}

QVariantMap rectMapF(const QRectF &rect)
{
    return QVariantMap{{QStringLiteral("x"), rect.x()}, {QStringLiteral("y"), rect.y()},
                       {QStringLiteral("width"), rect.width()}, {QStringLiteral("height"), rect.height()}};
}

} // namespace

QVariantMap BosunBackend::cropCenteredFor(int sourceWidth, int sourceHeight, int canvasWidth, int canvasHeight)
{
    return rectMap(crop::centered(QSize(sourceWidth, sourceHeight), QSize(canvasWidth, canvasHeight)));
}

QVariantMap BosunBackend::cropMovedFor(const QVariantMap &crop, int dx, int dy, int sourceWidth, int sourceHeight, int canvasWidth,
                                       int canvasHeight)
{
    const QRect moved = crop::moved(rectOf(crop), QPoint(dx, dy), QSize(sourceWidth, sourceHeight), QSize(canvasWidth, canvasHeight));
    return moved.isEmpty() ? crop : rectMap(moved);
}

QVariantMap BosunBackend::cropZoomedFor(const QVariantMap &crop, double factor, double anchorX, double anchorY, int sourceWidth, int sourceHeight,
                                        int canvasWidth, int canvasHeight)
{
    const QRect zoomed = crop::zoomed(rectOf(crop), factor, QPointF(anchorX, anchorY), QSize(sourceWidth, sourceHeight), QSize(canvasWidth, canvasHeight));
    return zoomed.isEmpty() ? crop : rectMap(zoomed);
}

QVariantMap BosunBackend::cropCentered() const
{
    return cropCenteredFor(candidateSource().width(), candidateSource().height(), canvas().width(), canvas().height());
}

QVariantMap BosunBackend::cropMoved(const QVariantMap &crop, int dx, int dy) const
{
    return cropMovedFor(crop, dx, dy, candidateSource().width(), candidateSource().height(), canvas().width(), canvas().height());
}

QVariantMap BosunBackend::cropZoomed(const QVariantMap &crop, double factor, double anchorX, double anchorY) const
{
    return cropZoomedFor(crop, factor, anchorX, anchorY, candidateSource().width(), candidateSource().height(), canvas().width(), canvas().height());
}

void BosunBackend::buildCandidate()
{
    m_candidate.clear();
    if (m_candidateChosen.isEmpty())
        return;
    QString state = QStringLiteral("unusable");
    QString why = m_candidateProblem;
    VideoAdvice advice;
    if (m_candidatePath.isEmpty()) {
        advice.notes << m_candidateProblem;
    } else if (m_probe.isAvailable() && !m_videoInfo.contains(m_candidatePath)) {
        state = QStringLiteral("checking");
        why = tr("Reading the video…");
    } else {
        const galleond::VideoInfo info = m_videoInfo.value(m_candidatePath);
        const galleond::ScreensaverConfig saver = m_config ? m_config->screensaver : galleond::ScreensaverConfig{};
        const galleond::ScreensaverSplitter splitter(saver);
        const QSize canvas = splitter.isValid() ? splitter.canvasSize() : QSize();
        if (m_probe.isAvailable()) {
            advice = adviseOn(info, canvas, saver.fps);
            if (!info.valid)
                why = tr("ffprobe could not read this video.");
        }
        state = (info.valid || !m_probe.isAvailable()) ? QStringLiteral("ready") : QStringLiteral("unusable");
        if (state == QStringLiteral("ready") && saver.videos.size() >= galleond::MaxScreensaverVideos)
            why = tr("The list is full (%1 videos): remove one first.").arg(galleond::MaxScreensaverVideos);
        else if (state == QStringLiteral("ready") && m_videoFiles.contains(m_candidatePath))
            why = tr("This video is already in the list.");
    }
    const QString picture = m_videoPictures.value(m_candidatePath);
    // What the crop window draws: the video, the picture of the module and where its screen and keys fall.
    const QSize source = candidateSource();
    const QSize frame = canvas();
    QVariantMap cut{{QStringLiteral("canCut"), false}};
    if (state == QStringLiteral("ready") && !source.isEmpty() && !frame.isEmpty()) {
        const galleond::ScreensaverConfig saver = m_config ? m_config->screensaver : galleond::ScreensaverConfig{};
        // Cutting makes a new file, so a video that is already in the list can be cut again; a full list cannot take it.
        const bool roomLeft = saver.videos.size() < galleond::MaxScreensaverVideos;
        const bool tool = m_converter.isAvailable();
        const auto regions = galleond::ScreensaverSplitter(saver).regions();
        QVariantList keys;
        for (const QRectF &key : regions.keys)
            keys.append(rectMapF(key));
        QString cutWhy;
        if (!tool)
            cutWhy = tr("FFmpeg was not found, or it has no H.264 or MPEG-4 encoder.");
        else if (!roomLeft)
            cutWhy = tr("The list is full (%1 videos): remove one first.").arg(galleond::MaxScreensaverVideos);
        cut = QVariantMap{{QStringLiteral("canCut"), tool && roomLeft},
                          {QStringLiteral("cutWhy"), cutWhy},
                          {QStringLiteral("width"), source.width()},
                          {QStringLiteral("height"), source.height()},
                          {QStringLiteral("canvasWidth"), frame.width()},
                          {QStringLiteral("canvasHeight"), frame.height()},
                          {QStringLiteral("screen"), rectMapF(regions.screen)},
                          {QStringLiteral("keyRegions"), keys},
                          {QStringLiteral("crop"), cropCentered()}};
    }
    m_candidate = QVariantMap{{QStringLiteral("path"), m_candidatePath},
                              {QStringLiteral("name"), QFileInfo(m_candidatePath.isEmpty() ? m_candidateChosen : m_candidatePath).fileName()},
                              {QStringLiteral("state"), state},
                              {QStringLiteral("details"), advice.details},
                              {QStringLiteral("notes"), advice.notes},
                              {QStringLiteral("thumbnail"), picture.isEmpty() ? QString() : QUrl::fromLocalFile(picture).toString()},
                              {QStringLiteral("canAdd"), state == QStringLiteral("ready") && why.isEmpty()},
                              {QStringLiteral("why"), why},
                              {QStringLiteral("suggestedTitle"), QFileInfo(m_candidatePath).completeBaseName()}};
    m_candidate.insert(cut);
}

const galleond::Page *BosunBackend::pageNamed(const QString &page) const
{
    if (!m_config)
        return nullptr;
    const auto it = m_config->pages.constFind(page);
    return it == m_config->pages.constEnd() ? nullptr : &it.value();
}

QStringList BosunBackend::pageNames() const
{
    return m_config ? m_config->pages.keys() : QStringList();
}

QStringList BosunBackend::subPagesOf(const QString &page) const
{
    QStringList found;
    if (!m_config)
        return found;
    QStringList themePages;
    for (const galleond::Theme &theme : m_config->themes)
        themePages << theme.page;
    QStringList toVisit{page};
    QStringList visited;
    while (!toVisit.isEmpty()) {
        const QString current = toVisit.takeFirst();
        if (visited.contains(current))
            continue;
        visited << current;
        const galleond::Page *looked = pageNamed(current);
        if (!looked)
            continue;
        for (const auto &key : looked->keys) {
            if (!key || key->action.type != ActionType::Page)
                continue;
            const QString target = key->action.page;
            if (target == page || themePages.contains(target) || found.contains(target))
                continue;
            found << target;
            toVisit << target;
        }
    }
    return found;
}

QVariantList BosunBackend::mediaCommands()
{
    QVariantList list;
    for (const MediaEntry &entry : MediaEntries)
        list.append(QVariantMap{{QStringLiteral("id"), QLatin1String(entry.id)}, {QStringLiteral("label"), tr(entry.words)}});
    return list;
}

QStringList BosunBackend::keypadKeys()
{
    return galleond::allowedKeyNames().split(QStringLiteral(", "));
}

QVariantList BosunBackend::installedApps()
{
    if (!m_appsRead) {
        for (const galleond::DesktopApp &app : galleond::DesktopEntries::list()) {
            m_apps.append(QVariantMap{{QStringLiteral("name"), app.name},
                                      {QStringLiteral("id"), app.id},
                                      {QStringLiteral("icon"), app.icon.isEmpty() ? app.iconPath : app.icon}});
        }
        m_appsRead = true;
    }
    return m_apps;
}

QString BosunBackend::setKey(const QString &page, int key, const QVariantMap &spec)
{
    galleond::KeySpec out;
    out.type = spec.value(QStringLiteral("type")).toString();
    out.desktop = spec.value(QStringLiteral("desktop")).toString();
    out.command = spec.value(QStringLiteral("command")).toString();
    out.step = spec.value(QStringLiteral("step")).toInt();
    out.seconds = spec.value(QStringLiteral("seconds")).toInt();
    out.page = spec.value(QStringLiteral("page")).toString();
    out.keyName = spec.value(QStringLiteral("keyName")).toString();
    out.appid = spec.value(QStringLiteral("appid")).toInt();
    out.label = spec.value(QStringLiteral("label")).toString();
    out.icon = spec.value(QStringLiteral("icon")).toString();
    return applied(galleond::PagesEditor::setKey(m_file, page, key, out));
}

QString BosunBackend::clearKey(const QString &page, int key)
{
    return applied(galleond::PagesEditor::clearKey(m_file, page, key));
}

QString BosunBackend::addTheme(const QString &name, const QString &color)
{
    return applied(galleond::PagesEditor::addTheme(m_file, name, color, pageNameFor(name, pageNames())));
}

QString BosunBackend::addSubPage(const QString &page)
{
    return applied(galleond::PagesEditor::addSubPage(m_file, page, pageNameFor(page + QStringLiteral(" plus"), pageNames()), tr("More"), tr("Back")));
}

QString BosunBackend::setTheme(int index, const QString &name, const QString &color)
{
    return applied(galleond::PagesEditor::setTheme(m_file, index, name, color));
}

QString BosunBackend::removeTheme(int index)
{
    return applied(galleond::PagesEditor::removeTheme(m_file, index));
}

QString BosunBackend::moveTheme(int index, int delta)
{
    return applied(galleond::PagesEditor::moveTheme(m_file, index, delta));
}

QVariantList BosunBackend::keys(const QString &page) const
{
    QVariantList result;
    const galleond::Page *found = pageNamed(page);
    if (!found)
        return result;
    const galleond::Page &configured = *found;
    for (int index = 0; index < KeyCount; ++index) {
        const auto &key = configured.keys.at(static_cast<std::size_t>(index));
        QString label;
        Described what{.kind = QStringLiteral("empty"), .detail = {}};
        if (key) {
            what = describe(*key);
            label = key->label;
            if (label.isEmpty() && key->action.type == ActionType::App) {
                const galleond::DesktopApp app = galleond::DesktopEntries::lookup(key->action.desktop);
                label = app.isValid() ? app.name : key->action.desktop;
            }
        }
        QVariantMap entry{{QStringLiteral("index"), index},
                          {QStringLiteral("label"), label},
                          {QStringLiteral("kind"), what.kind},
                          {QStringLiteral("detail"), what.detail},
                          {QStringLiteral("empty"), !key.has_value()}};
        if (key)
            entry.insert(configuredFields(*key));
        result.append(entry);
    }
    return result;
}

QString BosunBackend::autoFill(const QString &page) const
{
    const galleond::Page *found = pageNamed(page);
    if (!found)
        return {};
    const galleond::Page &configured = *found;
    QStringList parts;
    if (configured.sort == galleond::PageSort::Usage)
        parts << tr("applications ordered by use");
    if (configured.steamMax > 0)
        parts << tr("up to %n Steam game(s)", nullptr, configured.steamMax);
    if (configured.menuMax > 0)
        parts << tr("up to %n application(s) of the “%1” menu category", nullptr, configured.menuMax).arg(configured.menuCategory);
    return parts.join(QStringLiteral(", "));
}

QPixmap BosunBackend::tile(const QString &page, int key, const QString &themePage) const
{
    // The theme of the page tints its keys, as on the module.
    qint64 tint = -1;
    if (m_config) {
        for (const galleond::Theme &theme : m_config->themes) {
            if (theme.page == (themePage.isEmpty() ? page : themePage)) {
                tint = theme.rgb;
                break;
            }
        }
    }
    QImage image = galleond::TileRenderer::blank(tint);
    const galleond::Page *found = pageNamed(page);
    if (found && key >= 0 && key < KeyCount) {
        const auto &configured = found->keys.at(static_cast<std::size_t>(key));
        if (configured) {
            QString iconFile;
            const galleond::DesktopApp app = configured->action.type == ActionType::App
                                                 ? galleond::DesktopEntries::lookup(configured->action.desktop)
                                                 : galleond::DesktopApp{};
            const galleond::KeyConfig effective = galleond::TileRenderer::effectiveKey(*configured, app, iconFile);
            if (configured->action.type == ActionType::Steam && configured->icon.name.isEmpty())
                iconFile = galleond::SteamLibrary::coverImage(configured->action.appid);
            image = m_tiles.render(effective, QStringLiteral("stopped"), false, iconFile, tint);
        }
    }
    return QPixmap::fromImage(image);
}

TileProvider::TileProvider(const BosunBackend *backend)
    : QQuickImageProvider(QQuickImageProvider::Pixmap)
    , m_backend(backend)
{
}

QPixmap TileProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    Q_UNUSED(requestedSize)
    // "<page>/<key>" and maybe "?r=3&theme=<page>": anything else gives a blank key.
    const QString path = id.section(QLatin1Char('?'), 0, 0);
    QString themePage;
    for (const QString &pair : id.section(QLatin1Char('?'), 1).split(QLatin1Char('&'))) {
        if (pair.startsWith(QLatin1String("theme=")))
            themePage = pair.mid(6);
    }
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    bool ok = false;
    const int key = slash > 0 ? path.mid(slash + 1).toInt(&ok) : -1;
    QPixmap pixmap = m_backend->tile(ok ? path.left(slash) : QString(), ok ? key : -1, themePage);
    if (size)
        *size = pixmap.size();
    return pixmap;
}

} // namespace bosun
