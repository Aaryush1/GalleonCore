// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "AppsBackend.h"

#include "PagesConfig.h"
#include "PagesEditor.h"
#include "TileRenderer.h"

#include <QFile>
#include <QFileInfo>
#include <QIcon>
#include <QPainter>
#include <QVariantMap>

namespace galleond {

ThemeIconProvider::ThemeIconProvider()
    : QQuickImageProvider(QQuickImageProvider::Pixmap)
{
}

QPixmap ThemeIconProvider::requestPixmap(const QString &id, QSize *size, const QSize &requestedSize)
{
    const int side = requestedSize.isValid() && requestedSize.width() > 0 ? qMin(requestedSize.width(), 256) : 48;
    // `id` is the name of an icon of the theme, or the absolute path of an
    // application's own image (checked like any picture); anything that is
    // neither gives the generic application icon.
    QPixmap pixmap;
    if (id.startsWith(QLatin1Char('/'))) {
        pixmap = QPixmap::fromImage(TileRenderer::loadIconFile(id, side));
    } else {
        QIcon icon = QIcon::fromTheme(id);
        if (icon.isNull())
            icon = QIcon::fromTheme(QStringLiteral("application-x-executable"));
        pixmap = icon.pixmap(side, side);
    }
    if (pixmap.isNull()) {
        pixmap = QPixmap(side, side);
        pixmap.fill(QColor(0x40, 0x46, 0x50));
    }
    if (size)
        *size = pixmap.size();
    return pixmap;
}

AppsBackend::AppsBackend(QString pagesFile, QObject *parent)
    : QObject(parent)
    , m_file(std::move(pagesFile))
{
    for (const DesktopApp &app : DesktopEntries::list()) {
        m_apps.append(QVariantMap{{QStringLiteral("name"), app.name},
                                  {QStringLiteral("id"), app.id},
                                  {QStringLiteral("icon"), app.icon.isEmpty() ? app.iconPath : app.icon}});
    }
    reload();
}

void AppsBackend::setPage(const QString &page)
{
    if (page == m_page || !m_pages.contains(page))
        return;
    m_page = page;
    reload();
}

void AppsBackend::reload()
{
    QByteArray json = defaultPagesJson();
    if (QFileInfo::exists(m_file)) {
        QFile file(m_file);
        if (!file.open(QIODevice::ReadOnly)) {
            m_problem = tr("Cannot read %1").arg(m_file);
            m_keys = {};
            Q_EMIT changed();
            return;
        }
        json = file.read(MaxPagesFileBytes + 1);
    }

    const PagesParseResult parsed = parsePagesConfig(json);
    m_keys = {};
    if (!parsed.config) {
        m_problem = tr("The pages file has errors, fix them first:\n%1").arg(parsed.errors.join(QLatin1Char('\n')));
        Q_EMIT changed();
        return;
    }
    m_problem.clear();

    m_pages = parsed.config->pages.keys();
    if (!m_pages.contains(m_page))
        m_page = parsed.config->home;
    const Page &page = parsed.config->pages[m_page];
    for (int index = 0; index < galleon::protocol::KeyCount; ++index) {
        const auto &key = page.keys[static_cast<std::size_t>(index)];
        QString kind = QStringLiteral("empty");
        QString label;
        QString icon;
        if (key) {
            label = key->label;
            icon = key->icon.name.isEmpty() && !key->icon.byState.isEmpty() ? key->icon.byState.constBegin().value() : key->icon.name;
            switch (key->action.type) {
            case ActionType::App: {
                kind = QStringLiteral("app");
                const DesktopApp app = DesktopEntries::lookup(key->action.desktop);
                if (label.isEmpty())
                    label = app.isValid() ? app.name : key->action.desktop;
                if (icon.isEmpty())
                    icon = app.icon.isEmpty() ? app.iconPath : app.icon;
                break;
            }
            case ActionType::Media:
                kind = QStringLiteral("media");
                break;
            case ActionType::Page:
                kind = QStringLiteral("page");
                break;
            case ActionType::Back:
                kind = QStringLiteral("back");
                break;
            case ActionType::Key:
                kind = QStringLiteral("key");
                break;
            case ActionType::WallpaperMode:
                kind = QStringLiteral("wallpaper-mode");
                break;
            case ActionType::Theme:
                kind = QStringLiteral("theme");
                break;
            case ActionType::None:
                kind = QStringLiteral("none");
                break;
            }
        }
        m_keys.append(QVariantMap{{QStringLiteral("index"), index},
                                  {QStringLiteral("label"), label},
                                  {QStringLiteral("icon"), icon},
                                  {QStringLiteral("kind"), kind}});
    }
    Q_EMIT changed();
}

QString AppsBackend::assign(const QString &desktop, int key)
{
    const EditResult result = PagesEditor::assignApp(m_file, m_page, key, desktop);
    reload();
    return result.ok ? QString() : result.errors.join(QLatin1Char('\n'));
}

QString AppsBackend::clear(int key)
{
    const EditResult result = PagesEditor::clearKey(m_file, m_page, key);
    reload();
    return result.ok ? QString() : result.errors.join(QLatin1Char('\n'));
}

} // namespace galleond
