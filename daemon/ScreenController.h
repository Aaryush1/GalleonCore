// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

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
// minute. Exposed on D-Bus as org.galleoncore.Deck (the slots below) and to
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
    // `deck` and `renderer` are not owned and must outlive the controller.
    // `deck` may be null (preview without hardware).
    ScreenController(galleon::StreamDeck *deck,
                     ScreenRenderer *renderer,
                     WallpaperCatalog catalog,
                     QString widgetFile,
                     QObject *parent = nullptr);

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
    void tick();
    bool draw();
    void scheduleNextMinute();
    void checkDeck();

    galleon::StreamDeck *m_deck;
    ScreenRenderer *m_renderer;
    WallpaperCatalog m_catalog;
    QString m_widgetFile;
    QString m_wallpaper; // catalog name, empty when nothing is shown
    QTimer m_minuteTimer;
    QTimer m_deckTimer;
    bool m_wasAvailable = false;
};

} // namespace galleond
