// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScreenController.h"

#include "ScreenRenderer.h"

#include <galleon/Protocol.h>
#include <galleon/StreamDeck.h>

#include <algorithm>

#include <QColor>
#include <QDateTime>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QIcon>
#include <QDebug>
#include <QSettings>
#include <QVariantMap>

namespace galleond {

namespace {

constexpr int DeckCheckIntervalMs = 5000;
constexpr int MinuteMs = 60 * 1000;
constexpr int WallpaperHoldMs = 4000; // a chosen wallpaper is shown this long before a playing track takes the screen
constexpr int PreviewQuality = 65;    // while a dial chooses: smaller pictures, nothing queues up in the module
constexpr int NormalQuality = 90;
constexpr int PreviewIntervalMs = 20; // rest between two pictures while a dial turns fast (a picture takes about 45 ms)
// Wake up just after the minute changes.
constexpr int MinuteMarginMs = 50;
const QString SettingWallpaper = QStringLiteral("wallpaper");

QSettings settings()
{
    return QSettings(QStringLiteral("galleoncore"), QStringLiteral("galleond"));
}

} // namespace

ScreenController::ScreenController(galleon::StreamDeck *deck,
                                   ScreenRenderer *renderer,
                                   WallpaperCatalog catalog,
                                   QString widgetFile,
                                   MediaController *media,
                                   ScreenRenderer *nowPlayingRenderer,
                                   VideoThumbnails *thumbnails,
                                   QObject *parent)
    : QObject(parent)
    , m_deck(deck)
    , m_renderer(renderer)
    , m_catalog(std::move(catalog))
    , m_widgetFile(std::move(widgetFile))
    , m_media(media)
    , m_nowPlayingRenderer(nowPlayingRenderer)
    , m_thumbnails(thumbnails)
{
    if (m_media && m_nowPlayingRenderer && m_thumbnails) {
        connect(m_thumbnails, &VideoThumbnails::thumbnailReady, this, [this](const QString &video, const QString &picture) {
            // Still the track being played? Then the picture is its cover.
            if (QUrl(m_media->nowPlaying().url).toLocalFile() != video || !m_wantedCover.isEmpty())
                return;
            m_wantedCover = QUrl::fromLocalFile(picture).toString();
            m_cover.request(m_wantedCover);
            updateCoverImage();
            if (wantsNowPlaying())
                draw();
        });
    }
    if (m_media && m_nowPlayingRenderer) {
        connect(m_media, &MediaController::nowPlayingChanged, this, &ScreenController::onNowPlayingChanged);
        connect(&m_cover, &CoverLoader::coverChanged, this, [this] {
            updateCoverImage();
            if (wantsNowPlaying())
                draw();
        });
    }
    m_holdTimer.setSingleShot(true);
    m_holdTimer.setInterval(WallpaperHoldMs);
    m_countTimer.setInterval(1000);
    connect(&m_countTimer, &QTimer::timeout, this, [this] {
        if (m_countdown > 1) {
            --m_countdown;
            if (!m_wallpaper.isEmpty())
                draw();
        } else {
            m_countTimer.stop();
        }
    });
    connect(&m_holdTimer, &QTimer::timeout, this, [this] {
        m_holding = false;
        m_countdown = 0;
        m_countTimer.stop();
        if (!m_wallpaper.isEmpty())
            draw(); // a playing track takes the screen now
    });
    m_previewTimer.setSingleShot(true);
    m_previewTimer.setInterval(PreviewIntervalMs);
    connect(&m_previewTimer, &QTimer::timeout, this, [this] {
        if (!m_previewPending)
            return;
        showStepped();
        m_previewTimer.start(previewRest()); // rest again before the next one
    });
    m_minuteTimer.setSingleShot(true);
    connect(&m_minuteTimer, &QTimer::timeout, this, &ScreenController::tick);

    if (m_deck) {
        // The module may be plugged in later, or unplugged: keep looking for it.
        m_deckTimer.setInterval(DeckCheckIntervalMs);
        connect(&m_deckTimer, &QTimer::timeout, this, &ScreenController::checkDeck);
        connect(m_deck, &galleon::StreamDeck::disconnected, this, &ScreenController::checkDeck);
        m_deckTimer.start();
        m_wasAvailable = m_deck->isOpen();
    }
}

bool ScreenController::deckAvailable() const
{
    return m_deck && m_deck->isOpen();
}

QVariantList ScreenController::wallpapers() const
{
    QVariantList list;
    for (const WallpaperEntry &entry : m_catalog.entries()) {
        list.append(QVariantMap{{QStringLiteral("style"), entry.style},
                                {QStringLiteral("name"), entry.name},
                                {QStringLiteral("url"), QUrl::fromLocalFile(entry.path)}});
    }
    return list;
}

QStringList ScreenController::Wallpapers() const
{
    QStringList names;
    for (const WallpaperEntry &entry : m_catalog.entries())
        names.append(entry.name);
    return names;
}

void ScreenController::restore()
{
    const QString name = settings().value(SettingWallpaper).toString();
    if (name.isEmpty())
        return;
    if (!m_catalog.find(name)) {
        qWarning() << "galleond: the saved wallpaper" << name << "is not in the catalog: the screen is left as it is";
        return;
    }
    // Shown at once and kept for a moment: a track that is already playing (the
    // player answers a little later) takes the screen when that time is over.
    if (SetWallpaper(name))
        startHold();
}

bool ScreenController::SetWallpaper(const QString &name)
{
    if (!m_catalog.find(name)) {
        qWarning() << "galleond: unknown wallpaper" << name;
        return false;
    }
    m_wallpaper = name;
    qInfo().noquote() << "galleond: wallpaper" << name;
    settings().setValue(SettingWallpaper, name);
    Q_EMIT wallpaperChanged();

    const bool drawn = draw();
    scheduleNextMinute();
    return drawn;
}

void ScreenController::ClearWallpaper()
{
    m_minuteTimer.stop();
    m_wallpaper.clear();
    settings().remove(SettingWallpaper);
    Q_EMIT wallpaperChanged();
}

void ScreenController::tick()
{
    draw();
    scheduleNextMinute();
}

void ScreenController::scheduleNextMinute()
{
    if (m_wallpaper.isEmpty())
        return;
    const QTime now = QTime::currentTime();
    const int elapsed = (now.second() * 1000) + now.msec();
    m_minuteTimer.start(MinuteMs - elapsed + MinuteMarginMs);
}

void ScreenController::setScreenConfig(const ScreenConfig &config)
{
    const bool changed = !(config == m_screenConfig);
    m_screenConfig = config;
    m_cover.setRemoteAllowed(config.remoteCovers);
    m_holdTimer.setInterval(config.wallpaperHoldSeconds * 1000);
    if (changed && !m_wallpaper.isEmpty())
        draw();
}

void ScreenController::setTheme(const QString &name, std::uint32_t rgb, bool choosing)
{
    if (name == m_themeName && rgb == m_themeRgb && choosing == m_themeChoosing)
        return;
    m_themeName = name;
    m_themeRgb = rgb;
    m_themeChoosing = choosing;
    if (!m_wallpaper.isEmpty())
        draw();
}

void ScreenController::setSuspended(bool suspended)
{
    if (suspended == m_suspended)
        return;
    m_suspended = suspended;
    if (!suspended && !m_wallpaper.isEmpty())
        draw();
}

QString ScreenController::choosingLabel() const
{
    if (!m_choosingWallpaper)
        return {};
    // "portrait-01  10/24": the file's name and its place among all the wallpapers.
    const QList<WallpaperEntry> &entries = m_catalog.entries();
    for (qsizetype i = 0; i < entries.size(); ++i) {
        if (entries[i].name == m_wallpaper)
            return QStringLiteral("%1   %2/%3").arg(QFileInfo(entries[i].name).completeBaseName()).arg(i + 1).arg(entries.size());
    }
    return {};
}

void ScreenController::startHold(bool countdown)
{
    m_holding = true;
    m_countdown = 0;
    m_countTimer.stop();
    if (countdown) {
        // The seconds left are shown over the wallpaper while it is kept.
        m_countdown = std::max(1, m_holdTimer.interval() / 1000);
        m_countTimer.start();
    }
    qInfo() << "galleond: the chosen wallpaper stays on the screen for" << m_holdTimer.interval() / 1000 << "s";
    m_holdTimer.start();
}

void ScreenController::setBottomBar(std::uint32_t rgb, bool choosingWallpaper)
{
    if (rgb == m_barRgb && choosingWallpaper == m_choosingWallpaper)
        return;
    if (m_choosingWallpaper && !choosingWallpaper) {
        persistWallpaper();
        startHold(true); // the choice is made: show it with a countdown, then back to the track
    } else if (choosingWallpaper) {
        m_holdTimer.stop();
        m_countTimer.stop();
        m_countdown = 0;
        m_holding = false;
    }
    m_barRgb = rgb;
    m_choosingWallpaper = choosingWallpaper;
    if (!m_wallpaper.isEmpty())
        draw();
}

void ScreenController::stepWallpaper(int steps)
{
    const QList<WallpaperEntry> &entries = m_catalog.entries();
    const auto count = static_cast<int>(entries.size());
    if (count == 0 || steps == 0)
        return;
    int current = -1;
    for (int i = 0; i < count; ++i) {
        if (entries[i].name == m_wallpaper)
            current = i;
    }
    // Steps are bounded by the dial decoder; the modulo keeps any value in range.
    const int next = (((current < 0 ? 0 : current) + steps) % count + count) % count;
    m_wallpaper = entries[next].name; // the choice counts at once
    // The first step is drawn at once: the answer to a turn must be immediate, or the
    // user turns once more. Steps that come while a picture is being made or just after
    // it are gathered, and the last one is drawn as soon as the dial rests.
    if (m_previewTimer.isActive()) {
        m_previewPending = true;
        return;
    }
    showStepped();
    m_previewTimer.start(previewRest());
}

int ScreenController::previewRest() const
{
    // As long as the picture took, at least: the module is given time to show one before the
    // next arrives, so that turns never pile up in it. Bounded: the rest never feels long.
    return std::clamp(m_lastPreviewMs * 2, PreviewIntervalMs, 200);
}

void ScreenController::showStepped()
{
    m_previewPending = false;
    if (m_wallpaper.isEmpty())
        return;
    QElapsedTimer took;
    took.start();
    Q_EMIT wallpaperChanged();
    draw();
    scheduleNextMinute();
    m_lastPreviewMs = static_cast<int>(took.elapsed());
    qInfo().noquote() << "galleond: wallpaper" << m_wallpaper << "(" << m_lastPreviewMs << "ms )";
}

void ScreenController::persistWallpaper()
{
    // Written when the choice is made, not at each step of the dial.
    if (!m_wallpaper.isEmpty())
        settings().setValue(SettingWallpaper, m_wallpaper);
}

QVariantMap ScreenController::themeProperties() const
{
    return {{QStringLiteral("wallpaperLabel"), choosingLabel()},
            {QStringLiteral("countdown"), m_countdown},
            {QStringLiteral("barColor"), QColor::fromRgb(m_barRgb)},
            {QStringLiteral("themeChoosing"), m_themeChoosing},
            {QStringLiteral("themeName"), m_themeName},
            {QStringLiteral("themeColor"), QColor::fromRgb(m_themeRgb)}};
}

bool ScreenController::wantsNowPlaying() const
{
    if (m_choosingWallpaper || m_holding || !m_media || !m_nowPlayingRenderer || m_wallpaper.isEmpty() || m_screenConfig.nowPlaying == NowPlayingMode::Never)
        return false;
    if (m_media->nowPlaying().isEmpty())
        return false;
    const MediaController::State state = m_media->state();
    return state == MediaController::State::Playing
           || (m_screenConfig.nowPlaying == NowPlayingMode::PlayingOrPaused && state == MediaController::State::Paused);
}

QString ScreenController::thumbnailFor(const NowPlaying &track)
{
    if (!m_thumbnails || !m_screenConfig.videoThumbnails || !m_thumbnails->isAvailable())
        return {};
    const QUrl url(track.url);
    if (!url.isLocalFile() || !VideoThumbnails::isVideoFile(url.toLocalFile()))
        return {};
    const QString cached = m_thumbnails->cached(url.toLocalFile());
    if (!cached.isEmpty())
        return QUrl::fromLocalFile(cached).toString();
    m_thumbnails->request(url.toLocalFile(), track.durationSeconds); // answered by thumbnailReady
    return {};
}

QImage ScreenController::iconImage(const QString &iconName)
{
    if (iconName.isEmpty())
        return {};
    if (const auto known = m_icons.constFind(iconName); known != m_icons.constEnd())
        return known.value();
    const QIcon icon = QIcon::fromTheme(iconName);
    QImage image = icon.isNull() ? QImage() : icon.pixmap(256, 256).toImage();
    if (m_icons.size() >= 8)
        m_icons.clear(); // a session has few players: this only bounds the worst case
    m_icons.insert(iconName, image);
    return image;
}

void ScreenController::updateCoverImage()
{
    // The real cover first, then the player's icon; otherwise the widget's note.
    QImage image = m_cover.cover();
    QString key = image.isNull() ? QString() : QStringLiteral("cover:") + m_cover.coverUrl();
    bool isIcon = false;
    if (image.isNull()) {
        const QString iconName = m_media->nowPlaying().playerIcon;
        image = iconImage(iconName);
        isIcon = !image.isNull();
        key = isIcon ? QStringLiteral("icon:") + iconName : QString();
    }
    if (key == m_shownImageKey)
        return; // nothing new: keep the same id, so the widget does not decode it again
    m_shownImageKey = key;
    m_coverIsIcon = isIcon;
    m_coverId = image.isNull() ? QString() : QString::number(++m_coverSerial);
    if (!m_coverId.isEmpty())
        m_nowPlayingRenderer->setProvidedImage(m_coverId, image);
}

void ScreenController::onNowPlayingChanged()
{
    // A new cover is wanted whenever the track says another address.
    const NowPlaying track = m_media->nowPlaying();
    // The folder is listed once per track, not at every second of the timeline
    // (it may be on a slow drive).
    if (track.artUrl != m_lastArtUrl || track.url != m_lastTrackUrl) {
        m_lastArtUrl = track.artUrl;
        m_lastTrackUrl = track.url;
        m_wantedCover = track.artUrl.isEmpty() ? CoverLoader::findLocalCover(track.url) : track.artUrl;
        if (m_wantedCover.isEmpty())
            m_wantedCover = thumbnailFor(track);
    }
    m_cover.request(m_wantedCover);
    updateCoverImage(); // the player's icon may stand in, or the track changed
    if (!m_wallpaper.isEmpty())
        draw(); // the track, its timeline, or the return to the wallpaper
}

bool ScreenController::sendScreen(const QImage &frame)
{
    QElapsedTimer took;
    took.start();
    const bool sent = m_deck->setScreenImage(frame, m_choosingWallpaper ? PreviewQuality : NormalQuality);
    if (took.elapsed() > 150) // for finding what makes the dials slow to answer
        qInfo() << "galleond: sending the screen picture took" << took.elapsed() << "ms";
    if (!sent && (m_failedSends++ % 20) == 0)
        qWarning() << "galleond: the screen picture could not be sent to the module";
    return sent;
}

bool ScreenController::drawNowPlaying()
{
    const NowPlaying track = m_media->nowPlaying();
    QVariantMap properties = themeProperties();
    properties.insert({{QStringLiteral("title"), track.title},
         {QStringLiteral("artist"), track.artist},
         {QStringLiteral("positionSeconds"), track.positionSeconds},
         {QStringLiteral("durationSeconds"), track.durationSeconds},
         {QStringLiteral("playing"), m_media->state() == MediaController::State::Playing},
         {QStringLiteral("coverId"), m_coverId},
         {QStringLiteral("coverIsIcon"), m_coverIsIcon},
         {QStringLiteral("now"), QDateTime::currentDateTime()},
         {QStringLiteral("locale"), QVariant::fromValue(QLocale())}});
    const QImage frame = m_nowPlayingRenderer->render(
        properties, QSize(galleon::protocol::ScreenWidth, galleon::protocol::ScreenHeight));
    return !frame.isNull() && sendScreen(frame);
}

bool ScreenController::draw()
{
    const WallpaperEntry *entry = m_catalog.find(m_wallpaper);
    if (!entry || !deckAvailable() || m_suspended)
        return false;
    const bool nowPlaying = wantsNowPlaying();
    if (nowPlaying != m_showedNowPlaying) {
        m_showedNowPlaying = nowPlaying;
        qInfo() << "galleond: the screen shows" << (nowPlaying ? "the track" : "the wallpaper");
    }
    if (nowPlaying)
        return drawNowPlaying();

    QVariantMap properties = themeProperties();
    properties.insert({{QStringLiteral("wallpaper"), QUrl::fromLocalFile(entry->path)},
                       {QStringLiteral("now"), QDateTime::currentDateTime()},
                       {QStringLiteral("locale"), QVariant::fromValue(QLocale())}});
    const QImage frame = m_renderer->render(
        properties, QSize(galleon::protocol::ScreenWidth, galleon::protocol::ScreenHeight));
    if (frame.isNull())
        return false;
    return sendScreen(frame);
}

void ScreenController::checkDeck()
{
    if (!m_deck)
        return;
    if (!m_deck->isOpen())
        m_deck->open();

    const bool available = m_deck->isOpen();
    if (available == m_wasAvailable)
        return;
    m_wasAvailable = available;
    Q_EMIT deckAvailableChanged();
    if (available) {
        qInfo() << "galleond: Stream Deck module found";
        draw(); // a module just plugged in shows the wallpaper at once
    }
}

} // namespace galleond
