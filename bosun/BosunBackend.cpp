// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BosunBackend.h"

#include <DesktopEntries.h>
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

} // namespace

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
{
    qRegisterMetaType<galleond::VideoInfo>();
    connect(&m_probe, &galleond::VideoProbe::probed, this, [this](const QString &path, const galleond::VideoInfo &info) {
        m_videoInfo.insert(path, info);
        buildScreensaver();
        Q_EMIT changed();
        nextThumbnail(); // the length picks the moment of the picture
    });
    connect(&m_thumbnails, &galleond::VideoThumbnails::thumbnailReady, this, [this](const QString &path, const QString &picture) {
        m_videoPictures.insert(path, picture);
        m_pictureBusy = false;
        buildScreensaver();
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

    QByteArray json = galleond::defaultPagesJson();
    if (QFileInfo::exists(m_file)) {
        QFile file(m_file);
        if (!file.open(QIODevice::ReadOnly)) {
            m_problem = tr("Cannot read %1").arg(m_file);
            Q_EMIT changed();
            return;
        }
        json = file.read(galleond::MaxPagesFileBytes + 1);
    }
    const galleond::PagesParseResult parsed = galleond::parsePagesConfig(json);
    if (!parsed.config) {
        m_problem = tr("The pages file has errors:\n%1").arg(parsed.errors.join(QLatin1Char('\n')));
        Q_EMIT changed();
        return;
    }
    m_config = parsed.config;
    buildScreensaver();
    askAboutVideos();
    for (const galleond::Theme &theme : parsed.config->themes) {
        m_themes.append(QVariantMap{{QStringLiteral("name"), theme.name},
                                    {QStringLiteral("page"), theme.page},
                                    {QStringLiteral("color"), QColor::fromRgb(theme.rgb).name()}});
    }
    Q_EMIT changed();
}

namespace {

QString durationText(double seconds)
{
    if (seconds <= 0)
        return {};
    const int whole = static_cast<int>(std::lround(seconds));
    return whole >= 60 ? QStringLiteral("%1:%2").arg(whole / 60).arg(whole % 60, 2, 10, QLatin1Char('0'))
                       : QStringLiteral("%1 s").arg(whole);
}

} // namespace

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
            const galleond::VideoInfo info = m_videoInfo.value(path);
            if (!info.valid) {
                notes << tr("ffprobe could not read this video.");
            } else {
                QStringList parts{QStringLiteral("%1 × %2").arg(info.width).arg(info.height)};
                if (info.fps > 0)
                    parts << tr("%1 fps").arg(info.fps, 0, 'f', 1);
                if (info.seconds > 0)
                    parts << durationText(info.seconds);
                if (!info.codec.isEmpty())
                    parts << info.codec;
                details = parts.join(QStringLiteral(" · "));
                if (!canvas.isEmpty()) {
                    // The module scales the video to cover the picture and cuts the middle.
                    const double videoRatio = static_cast<double>(info.width) / info.height;
                    const double canvasRatio = static_cast<double>(canvas.width()) / canvas.height();
                    const double cut = videoRatio > canvasRatio ? 1 - (canvasRatio / videoRatio) : 1 - (videoRatio / canvasRatio);
                    if (cut >= 0.05) {
                        const QString where = videoRatio > canvasRatio ? tr("at the sides") : tr("at the top and bottom");
                        notes << tr("About %1% of the picture is cut off %2 (the module needs the proportions of %3 × %4).")
                                     .arg(std::lround(cut * 100))
                                     .arg(where)
                                     .arg(canvas.width())
                                     .arg(canvas.height());
                    }
                    if (info.width < (canvas.width() * 9) / 10)
                        notes << tr("Smaller than the screen (%1 px wide): it will be enlarged.").arg(canvas.width());
                }
                if (info.fps > 0 && info.fps < config.fps - 0.5)
                    notes << tr("The video has fewer images per second than the setting (%1): none are made up.").arg(config.fps);
            }
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
    for (const QString &path : std::as_const(m_videoFiles)) {
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

const galleond::Page *BosunBackend::pageNamed(const QString &page) const
{
    if (!m_config)
        return nullptr;
    const auto it = m_config->pages.constFind(page);
    return it == m_config->pages.constEnd() ? nullptr : &it.value();
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
        result.append(QVariantMap{{QStringLiteral("index"), index},
                                  {QStringLiteral("label"), label},
                                  {QStringLiteral("kind"), what.kind},
                                  {QStringLiteral("detail"), what.detail},
                                  {QStringLiteral("empty"), !key.has_value()}});
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

QPixmap BosunBackend::tile(const QString &page, int key) const
{
    // The theme of the page tints its keys, as on the module.
    qint64 tint = -1;
    if (m_config) {
        for (const galleond::Theme &theme : m_config->themes) {
            if (theme.page == page) {
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
            image = m_tiles.render(effective, {}, false, iconFile, tint);
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
    // "<page>/<key>" and maybe "?something": anything else gives a blank key.
    const QString path = id.section(QLatin1Char('?'), 0, 0);
    const qsizetype slash = path.lastIndexOf(QLatin1Char('/'));
    bool ok = false;
    const int key = slash > 0 ? path.mid(slash + 1).toInt(&ok) : -1;
    QPixmap pixmap = m_backend->tile(ok ? path.left(slash) : QString(), ok ? key : -1);
    if (size)
        *size = pixmap.size();
    return pixmap;
}

} // namespace bosun
