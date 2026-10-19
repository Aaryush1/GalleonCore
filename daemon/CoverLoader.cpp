// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CoverLoader.h"

#include <QBuffer>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QHostAddress>
#include <QImageReader>
#include <QNetworkReply>
#include <QNetworkRequest>

namespace galleond {

bool CoverLoader::isAllowedUrl(const QUrl &url, bool allowRemote)
{
    if (!url.isValid())
        return false;
    const QString scheme = url.scheme().toLower();
    if (scheme == QLatin1String("file"))
        return url.host().isEmpty() && !url.toLocalFile().isEmpty();
    if (scheme != QLatin1String("https") || !allowRemote)
        return false;

    const QString host = url.host().toLower();
    if (host.isEmpty() || !url.userInfo().isEmpty())
        return false;
    // No IP address, in any notation, and no name that is not on the Internet.
    if (!QHostAddress(host).isNull() || host.startsWith(QLatin1Char('[')) || host.contains(QLatin1Char(':')))
        return false;
    bool numeric = true; // 2130706433, 0x7f000001: forms an IP parser may accept
    for (const QChar c : host) {
        if (!(c.isDigit() || c == QLatin1Char('x') || c == QLatin1Char('.')))
            numeric = false;
    }
    if (numeric)
        return false;
    for (const QString &suffix : {QStringLiteral(".local"), QStringLiteral(".internal"), QStringLiteral(".lan"),
                                  QStringLiteral(".localhost"), QStringLiteral(".home"), QStringLiteral(".corp")}) {
        if (host.endsWith(suffix))
            return false;
    }
    return host != QLatin1String("localhost") && host.contains(QLatin1Char('.'));
}

QImage CoverLoader::decode(const QByteArray &bytes)
{
    if (bytes.isEmpty() || bytes.size() > MaxBytes)
        return {};
    QBuffer buffer;
    buffer.setData(bytes);
    if (!buffer.open(QIODevice::ReadOnly))
        return {};
    QImageReader reader(&buffer);
    reader.setDecideFormatFromContent(true);
    const QByteArray format = reader.format();
    if (format != "jpeg" && format != "png" && format != "webp")
        return {};
    const QSize size = reader.size();
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0 || size.width() > MaxDimension
        || size.height() > MaxDimension)
        return {};
    // Decode straight to the stored size: a 4096 x 4096 original costs little.
    reader.setScaledSize(size.scaled(MaxSide, MaxSide, Qt::KeepAspectRatio));
    return reader.read();
}

CoverLoader::CoverLoader(QObject *parent, Fetcher fetcher)
    : QObject(parent)
    , m_fetcher(std::move(fetcher))
{
    if (!m_fetcher) {
        m_fetcher = [this](const QUrl &url, const std::function<void(const QByteArray &)> &done) {
            fetchWithNetwork(url, done);
        };
    }
}

void CoverLoader::remember(const QString &url, const QImage &image)
{
    if (image.isNull())
        return;
    if (!m_cache.contains(url)) {
        m_cacheOrder.append(url);
        while (m_cacheOrder.size() > CacheEntries)
            m_cache.remove(m_cacheOrder.takeFirst());
    }
    m_cache.insert(url, image);
}

void CoverLoader::finish(const QString &url, const QImage &image)
{
    if (url != m_url) // the track changed while this was loading
        return;
    remember(url, image);
    m_cover = image;
    Q_EMIT coverChanged();
}

void CoverLoader::request(const QString &artUrl)
{
    if (artUrl == m_url)
        return;
    m_url = artUrl;
    const bool hadCover = !m_cover.isNull();
    m_cover = {};

    const QUrl url(artUrl);
    if (artUrl.isEmpty() || !isAllowedUrl(url, m_remoteAllowed)) {
        if (hadCover)
            Q_EMIT coverChanged();
        return;
    }
    if (const auto cached = m_cache.constFind(artUrl); cached != m_cache.constEnd()) {
        m_cover = cached.value();
        Q_EMIT coverChanged();
        return;
    }

    if (url.isLocalFile()) {
        const QFileInfo info(url.toLocalFile());
        QImage image;
        if (info.isFile() && info.size() > 0 && info.size() <= MaxBytes) {
            QFile file(info.absoluteFilePath());
            if (file.open(QIODevice::ReadOnly))
                image = decode(file.read(MaxBytes + 1));
        }
        finish(artUrl, image);
        return;
    }

    // A remote cover: the previous one is gone from the screen until this arrives.
    if (hadCover)
        Q_EMIT coverChanged();
    m_fetcher(url, [this, artUrl](const QByteArray &bytes) { finish(artUrl, decode(bytes)); });
}

void CoverLoader::fetchWithNetwork(const QUrl &url, const std::function<void(const QByteArray &)> &done)
{
    QNetworkRequest request(url);
    request.setTransferTimeout(TimeoutMs);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setMaximumRedirectsAllowed(MaxRedirects);
    request.setRawHeader("User-Agent", "GalleonCore");
    request.setAttribute(QNetworkRequest::CookieSaveControlAttribute, QNetworkRequest::Manual);
    request.setAttribute(QNetworkRequest::CookieLoadControlAttribute, QNetworkRequest::Manual);

    QNetworkReply *reply = m_network.get(request);
    // Every hop must still be an allowed address, and the body is capped.
    connect(reply, &QNetworkReply::redirected, reply, [this, reply](const QUrl &target) {
        if (!isAllowedUrl(target, m_remoteAllowed))
            reply->abort();
    });
    connect(reply, &QNetworkReply::downloadProgress, reply, [reply](qint64 received, qint64) {
        if (received > MaxBytes)
            reply->abort();
    });
    connect(reply, &QNetworkReply::finished, this, [reply, done] {
        reply->deleteLater();
        const bool ok = reply->error() == QNetworkReply::NoError
                        && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200;
        done(ok ? reply->read(MaxBytes + 1) : QByteArray());
    });
}

} // namespace galleond
