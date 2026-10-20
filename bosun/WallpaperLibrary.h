// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <WallpaperCatalog.h>

#include <QHash>
#include <QImage>
#include <QMutex>
#include <QObject>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

namespace bosun {

// The wallpapers of the module's screen as the settings window shows them: the pictures of
// the catalog (the project's, the system's and the user's own), the one in use, choosing one,
// and adding a picture of one's own, cropped to the screen. The pictures of the user go to
// ~/.local/share/galleoncore/wallpaper/mine/ and nowhere else; nothing here touches the
// project's own pictures. Every picture read here is a file chosen by a person and is checked
// (size, dimensions, format) before it is decoded.
class WallpaperLibrary : public QObject
{
    Q_OBJECT
    // [{name, style, mine, thumbnail}], in the order of the module's dial.
    Q_PROPERTY(QVariantList wallpapers READ wallpapers NOTIFY changed)
    // The name of the wallpaper in use (the one galleond keeps from one start to the next).
    Q_PROPERTY(QString current READ current NOTIFY changed)
    // The picture being looked at before it is added (see inspect()).
    Q_PROPERTY(QVariantMap candidate READ candidate NOTIFY changed)

public:
    static constexpr int ScreenWidth = 720;  // the screen of the module
    static constexpr int ScreenHeight = 384;
    static constexpr int SavedFactor = 2;    // a saved picture is twice the screen, for a sharp result
    static constexpr qint64 MaxFileBytes = 32LL * 1024 * 1024;
    static constexpr int MaxDimension = 8192;
    static constexpr int PreviewSide = 640;
    static constexpr int ThumbnailWidth = 320;

    // Where things are; tests give their own.
    struct Places
    {
        QStringList roots;     // the folders of the catalog
        QString userRoot;      // where the pictures of the user go
        QString settingsFile;  // galleond's settings; empty: the real ones (~/.config/galleoncore/galleond.conf)
    };
    [[nodiscard]] static Places defaultPlaces(const QString &buildTree = {});

    explicit WallpaperLibrary(Places places, QObject *parent = nullptr);

    [[nodiscard]] QVariantList wallpapers() const { return m_wallpapers; }
    [[nodiscard]] QString current() const;
    [[nodiscard]] QVariantMap candidate() const { return m_candidate; }

    // Uses the wallpaper `name` from now on: kept for the next start and, when galleond runs,
    // shown at once. "" when done, else why not.
    Q_INVOKABLE [[nodiscard]] QString apply(const QString &name);
    // Deletes one of the user's own pictures. The project's cannot be deleted, nor the one in use.
    Q_INVOKABLE [[nodiscard]] QString removeMine(const QString &name);
    Q_INVOKABLE void reload();

    // Looks at a picture the user chose (a path or a file:// address); nothing is changed.
    Q_INVOKABLE void inspect(const QString &pathOrUrl);
    Q_INVOKABLE void clearCandidate();
    // Cuts the part (x, y, width, height) of the picture being looked at, in its own pixels (made
    // valid first), and saves it under `name` among the user's pictures. "" when done.
    Q_INVOKABLE [[nodiscard]] QString addCandidate(int x, int y, int width, int height, const QString &name);

    // For the image providers (called from other threads).
    [[nodiscard]] QImage thumbnail(const QString &name) const;
    [[nodiscard]] QImage candidatePreview() const;

    // The picture of `path` made at most `side` pixels on its longest side, checked first; null
    // when it is not a picture worth reading.
    [[nodiscard]] static QImage readChecked(const QString &path, int side);

Q_SIGNALS:
    void changed();

private:
    void setCandidate(const QString &chosen, const QString &why);
    [[nodiscard]] bool isMine(const QString &path) const;

    Places m_places;
    galleond::WallpaperCatalog m_catalog;
    QVariantList m_wallpapers;
    QVariantMap m_candidate;
    QString m_candidatePath;
    int m_serial = 0; // makes the preview of each candidate a new address

    mutable QMutex m_lock; // guards what the providers read
    QHash<QString, QString> m_paths;
    QImage m_preview;
};

// image://wallpapers/<name>: a thumbnail of a wallpaper of the catalog.
class WallpaperThumbnails : public QQuickImageProvider
{
public:
    explicit WallpaperThumbnails(const WallpaperLibrary *library);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    const WallpaperLibrary *m_library;
};

// image://wallpaperpreview/<anything>: the picture being looked at.
class WallpaperPreview : public QQuickImageProvider
{
public:
    explicit WallpaperPreview(const WallpaperLibrary *library);
    QImage requestImage(const QString &id, QSize *size, const QSize &requestedSize) override;

private:
    const WallpaperLibrary *m_library;
};

} // namespace bosun
