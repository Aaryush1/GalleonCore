// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScreenController.h"

#include "ScreenRenderer.h"

#include <galleon/Protocol.h>
#include <galleon/StreamDeck.h>

#include <QDateTime>
#include <QDebug>
#include <QSettings>
#include <QVariantMap>

namespace galleond {

namespace {

constexpr int DeckCheckIntervalMs = 5000;
constexpr int MinuteMs = 60 * 1000;
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
                                   QObject *parent)
    : QObject(parent)
    , m_deck(deck)
    , m_renderer(renderer)
    , m_catalog(std::move(catalog))
    , m_widgetFile(std::move(widgetFile))
    , m_media(media)
    , m_nowPlayingRenderer(nowPlayingRenderer)
{
    if (m_media && m_nowPlayingRenderer) {
        connect(m_media, &MediaController::nowPlayingChanged, this, &ScreenController::onNowPlayingChanged);
        connect(&m_cover, &CoverLoader::coverChanged, this, [this] {
            m_coverId = m_cover.cover().isNull() ? QString() : QString::number(++m_coverSerial);
            if (!m_coverId.isEmpty())
                m_nowPlayingRenderer->setProvidedImage(m_coverId, m_cover.cover());
            if (wantsNowPlaying())
                draw();
        });
    }
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
    if (!name.isEmpty() && m_catalog.find(name))
        SetWallpaper(name);
}

bool ScreenController::SetWallpaper(const QString &name)
{
    if (!m_catalog.find(name)) {
        qWarning() << "galleond: unknown wallpaper" << name;
        return false;
    }
    m_wallpaper = name;
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
    if (changed && !m_wallpaper.isEmpty())
        draw();
}

bool ScreenController::wantsNowPlaying() const
{
    if (!m_media || !m_nowPlayingRenderer || m_wallpaper.isEmpty() || m_screenConfig.nowPlaying == NowPlayingMode::Never)
        return false;
    if (m_media->nowPlaying().isEmpty())
        return false;
    const MediaController::State state = m_media->state();
    return state == MediaController::State::Playing
           || (m_screenConfig.nowPlaying == NowPlayingMode::PlayingOrPaused && state == MediaController::State::Paused);
}

void ScreenController::onNowPlayingChanged()
{
    // A new cover is wanted whenever the track says another address.
    m_cover.request(m_media->nowPlaying().artUrl);
    if (!m_wallpaper.isEmpty())
        draw(); // the track, its timeline, or the return to the wallpaper
}

bool ScreenController::drawNowPlaying()
{
    const NowPlaying track = m_media->nowPlaying();
    const QImage frame = m_nowPlayingRenderer->render(
        {{QStringLiteral("title"), track.title},
         {QStringLiteral("artist"), track.artist},
         {QStringLiteral("positionSeconds"), track.positionSeconds},
         {QStringLiteral("durationSeconds"), track.durationSeconds},
         {QStringLiteral("playing"), m_media->state() == MediaController::State::Playing},
         {QStringLiteral("coverId"), m_coverId},
         {QStringLiteral("now"), QDateTime::currentDateTime()},
         {QStringLiteral("locale"), QVariant::fromValue(QLocale())}},
        QSize(galleon::protocol::ScreenWidth, galleon::protocol::ScreenHeight));
    return !frame.isNull() && m_deck->setScreenImage(frame);
}

bool ScreenController::draw()
{
    const WallpaperEntry *entry = m_catalog.find(m_wallpaper);
    if (!entry || !deckAvailable())
        return false;
    const bool nowPlaying = wantsNowPlaying();
    if (nowPlaying != m_showedNowPlaying) {
        m_showedNowPlaying = nowPlaying;
        qInfo() << "galleond: the screen shows" << (nowPlaying ? "the track" : "the wallpaper");
    }
    if (nowPlaying)
        return drawNowPlaying();

    const QImage frame = m_renderer->render(entry->path,
                                            QDateTime::currentDateTime(),
                                            QSize(galleon::protocol::ScreenWidth, galleon::protocol::ScreenHeight));
    if (frame.isNull())
        return false;
    return m_deck->setScreenImage(frame);
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
