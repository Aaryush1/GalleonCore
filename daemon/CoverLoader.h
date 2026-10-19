// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QNetworkAccessManager>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>

namespace galleond {

// Loads the cover art of the track being played.
//
// The address comes from the music player (a web page can set it through a
// browser), so it is untrusted:
//  - file:// : a regular file of the local disk, not too large;
//  - https:// only, never http, and never an IP address or a local name
//    (localhost, .local, .internal, .lan, a name without a dot), so that a page
//    cannot make the daemon contact the machines of the user's network;
//  - anything else (ftp:, data:, javascript:...) is refused.
// A downloaded image is limited in size and time, redirects stay https, and
// whatever the source, the bytes are decoded as a JPEG, PNG or WebP of sane
// dimensions and scaled down: nothing else is ever shown. Decoded covers are
// kept in a small cache.
class CoverLoader : public QObject
{
    Q_OBJECT

public:
    static constexpr qint64 MaxBytes = 4LL * 1024 * 1024;
    static constexpr int MaxDimension = 4096; // of the original, in pixels
    static constexpr int MaxSide = 512;       // the stored cover is scaled to fit
    static constexpr int CacheEntries = 8;
    static constexpr int MaxRedirects = 3;
    static constexpr int TimeoutMs = 5000;

    // Fetches a URL and hands the bytes (empty on failure) to the callback.
    using Fetcher = std::function<void(const QUrl &, std::function<void(const QByteArray &)>)>;

    // The default fetcher uses Qt Network; tests give their own.
    explicit CoverLoader(QObject *parent = nullptr, Fetcher fetcher = {});

    // False: remote covers are not downloaded, local ones still load.
    void setRemoteAllowed(bool allowed) { m_remoteAllowed = allowed; }

    // The cover of this address; an empty or refused address clears it.
    void request(const QString &artUrl);

    [[nodiscard]] QImage cover() const { return m_cover; }
    [[nodiscard]] QString coverUrl() const { return m_url; }

    // For a track that gives no cover: an image next to the local file being
    // played (cover.jpg, folder.png, front.jpg, albumart.jpg, album.jpg, or one
    // named like the file), as a file:// address; empty if there is none or the
    // track is not a local file. Only these names are looked for.
    [[nodiscard]] static QString findLocalCover(const QString &mediaUrl);

    [[nodiscard]] static bool isAllowedUrl(const QUrl &url, bool allowRemote = true);
    // The image in `bytes`, or a null image if it is not a sane JPEG, PNG or WebP.
    [[nodiscard]] static QImage decode(const QByteArray &bytes);

Q_SIGNALS:
    void coverChanged();

private:
    void finish(const QString &url, const QImage &image);
    void remember(const QString &url, const QImage &image);
    void fetchWithNetwork(const QUrl &url, const std::function<void(const QByteArray &)> &done);

    Fetcher m_fetcher;
    QNetworkAccessManager m_network;
    bool m_remoteAllowed = true;
    QString m_url;   // the address wanted now
    QImage m_cover;  // its picture, null while loading or if it failed
    QHash<QString, QImage> m_cache;
    QStringList m_cacheOrder; // oldest first
};

} // namespace galleond
