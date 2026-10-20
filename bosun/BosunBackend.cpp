// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BosunBackend.h"

#include <DesktopEntries.h>
#include <SteamLibrary.h>

#include <QColor>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QVariantMap>

#include <array>

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
    : QObject(parent)
    , m_file(std::move(pagesFile))
    , m_tiles(QFileInfo(m_file).absolutePath())
{
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
    for (const galleond::Theme &theme : m_config->themes) {
        m_themes.append(QVariantMap{{QStringLiteral("name"), theme.name},
                                    {QStringLiteral("page"), theme.page},
                                    {QStringLiteral("color"), QColor::fromRgb(theme.rgb).name()}});
    }
    Q_EMIT changed();
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
