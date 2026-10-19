// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "CoverLoader.h"
#include "MediaController.h"
#include "PagesConfig.h"
#include "VideoThumbnails.h"
#include "WallpaperCatalog.h"

#include <QObject>
#include <QString>
#include <QTimer>
#include <QUrl>
#include <QVariantList>

namespace galleon {
class StreamDeck;
}

namespace galleond {

class ScreenRenderer;

// Keeps the Galleon's screen showing a wallpaper with the time, redrawn every
// minute, or, while a player is playing (as the pages file allows), the track
// with its cover and timeline, redrawn every second. Exposed on D-Bus as org.galleoncore.Deck (the slots below) and to
// the preview window (the properties).
//
// Only a name from the catalog is ever accepted: D-Bus callers cannot make
// galleond open an arbitrary file.
class ScreenController : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.galleoncore.Deck")
    Q_PROPERTY(QString wallpaper READ wallpaper NOTIFY wallpaperChanged)
    Q_PROPERTY(bool deckAvailable READ deckAvailable NOTIFY deckAvailableChanged)
    Q_PROPERTY(QVariantList wallpapers READ wallpapers CONSTANT)
    Q_PROPERTY(QUrl widgetUrl READ widgetUrl CONSTANT)

public:
    // `deck`, `renderer`, `media` and `nowPlayingRenderer` are not owned and
    // must outlive the controller. `deck` may be null (preview without
    // hardware); `media` and `nowPlayingRenderer` may be null (no track screen).
    ScreenController(galleon::StreamDeck *deck,
                     ScreenRenderer *renderer,
                     WallpaperCatalog catalog,
                     QString widgetFile,
                     MediaController *media = nullptr,
                     ScreenRenderer *nowPlayingRenderer = nullptr,
                     VideoThumbnails *thumbnails = nullptr,
                     QObject *parent = nullptr);

    // The line along the top of the screen: the theme's name and colour
    // (0xRRGGBB). An empty name removes it.
    void setTheme(const QString &name, std::uint32_t rgb, bool choosing = false);

    // The line along the bottom edge (0xRRGGBB), and whether the wallpaper is
    // being chosen: then the wallpaper is shown even while a track plays.
    void setBottomBar(std::uint32_t rgb, bool choosingWallpaper);

    // Shows the wallpaper `steps` places further in the catalog (negative:
    // back), wrapping around. Remembered like any choice.
    void stepWallpaper(int steps);

    // While suspended (the screensaver runs) nothing is drawn; leaving it
    // draws the screen again.
    void setSuspended(bool suspended);

    // How long the chosen wallpaper stays on the screen, before a playing track
    // takes it back: after choosing a wallpaper, and at start. For the tests.
    void setWallpaperHoldMs(int ms) { m_holdTimer.setInterval(ms); }

    // When to show the track, and whether covers may be downloaded.
    void setScreenConfig(const ScreenConfig &config);

    [[nodiscard]] QString wallpaper() const { return m_wallpaper; }
    [[nodiscard]] bool deckAvailable() const;
    [[nodiscard]] QVariantList wallpapers() const;
    [[nodiscard]] QUrl widgetUrl() const { return QUrl::fromLocalFile(m_widgetFile); }

    // Takes the last wallpaper chosen, if it is still in the catalog.
    void restore();

public Q_SLOTS:
    // Shows this wallpaper (a catalog name such as "manga-classic/manga-classic-01.jpg").
    // False if the name is unknown or the frame could not be drawn.
    bool SetWallpaper(const QString &name);
    // Stops drawing; the screen keeps what it shows.
    void ClearWallpaper();
    [[nodiscard]] QStringList Wallpapers() const;

Q_SIGNALS:
    void wallpaperChanged();
    void deckAvailableChanged();

private:
    [[nodiscard]] QVariantMap themeProperties() const;
    void tick();
    bool draw();
    bool sendScreen(const QImage &frame);
    int m_failedSends = 0;
    std::uint32_t m_barRgb = 0xffffff;
    bool m_suspended = false;
    bool m_choosingWallpaper = false;
    bool m_themeChoosing = false;
    QString m_themeName;
    std::uint32_t m_themeRgb = 0;
    [[nodiscard]] bool wantsNowPlaying() const;
    bool drawNowPlaying();
    void onNowPlayingChanged();
    // Picks what the cover square shows: the cover, else the player's icon.
    void updateCoverImage();
    [[nodiscard]] QImage iconImage(const QString &iconName);
    // A picture of the local video being played, from the cache; if it is not
    // there yet, FFmpeg is asked for it and thumbnailReady comes later.
    [[nodiscard]] QString thumbnailFor(const NowPlaying &track);
    void scheduleNextMinute();
    void checkDeck();

    galleon::StreamDeck *m_deck;
    ScreenRenderer *m_renderer;
    WallpaperCatalog m_catalog;
    QString m_widgetFile;
    MediaController *m_media;
    ScreenRenderer *m_nowPlayingRenderer;
    VideoThumbnails *m_thumbnails;
    CoverLoader m_cover;
    ScreenConfig m_screenConfig;
    QString m_coverId; // changes with every cover, so the widget never reuses an old one
    QString m_shownImageKey; // what m_coverId stands for: "cover:<url>" or "icon:<name>"
    bool m_coverIsIcon = false;
    QHash<QString, QImage> m_icons; // theme icons already turned into images (a handful)
    int m_coverSerial = 0;
    QString m_lastArtUrl;   // the track the cover address was worked out for
    QString m_lastTrackUrl;
    QString m_wantedCover;  // that address: the player's, or an image next to the file
    bool m_showedNowPlaying = false; // for the log: what the screen last showed
    QString m_wallpaper; // catalog name, empty when nothing is shown
    QTimer m_minuteTimer;
    QTimer m_holdTimer;   // the wallpaper is kept for a moment, then the track returns
    bool m_holding = false;
    void startHold(bool countdown = false);
    [[nodiscard]] QString choosingLabel() const;
    QTimer m_countTimer; // ticks each second of the hold after a choice
    int m_countdown = 0; // seconds left, shown over the wallpaper; 0 = none
    QTimer m_previewTimer; // groups the wallpaper steps of a turning dial into few pictures
    void showStepped();
    QTimer m_deckTimer;
    bool m_wasAvailable = false;
};

} // namespace galleond
