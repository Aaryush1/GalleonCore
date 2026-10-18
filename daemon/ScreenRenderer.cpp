// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScreenRenderer.h"

#include <QDebug>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickRenderControl>
#include <QQuickRenderTarget>
#include <QQuickWindow>
#include <QSGRendererInterface>
#include <QUrl>

namespace galleond {

struct ScreenRenderer::Impl
{
    // Destroyed in reverse order: the item, then the window, the render
    // control, and the engine last.
    QQmlEngine engine;
    std::unique_ptr<QQuickRenderControl> control;
    std::unique_ptr<QQuickWindow> window;
    std::unique_ptr<QQuickItem> root;
    // The software renderer repaints only what changed, on the same image.
    QImage frame;
    bool valid = false;

    Impl()
    {
        // Must come before anything that starts the scene graph, which the
        // render control does as soon as it exists.
        QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
        control = std::make_unique<QQuickRenderControl>();
        window = std::make_unique<QQuickWindow>(control.get());
    }
};

ScreenRenderer::ScreenRenderer(const QString &widgetFile)
    : m_impl(std::make_unique<Impl>())
{
    QQmlComponent component(&m_impl->engine, QUrl::fromLocalFile(widgetFile));
    if (component.isError()) {
        qWarning() << "galleond: cannot load the screen widget" << widgetFile << component.errors();
        return;
    }
    std::unique_ptr<QObject> object(component.create());
    auto *item = qobject_cast<QQuickItem *>(object.get());
    if (!item) {
        qWarning() << "galleond: the screen widget is not an Item:" << widgetFile;
        return;
    }
    object.release();
    m_impl->root.reset(item);
    m_impl->root->setParentItem(m_impl->window->contentItem());
    // The scene is driven by hand: no timer inside the widget.
    m_impl->root->setProperty("live", false);

    // The software scene graph needs no QRhi: QQuickRenderControl::initialize()
    // is for the hardware backends and refuses the software one.
    m_impl->valid = true;
}

ScreenRenderer::~ScreenRenderer() = default;

bool ScreenRenderer::isValid() const
{
    return m_impl->valid;
}

QImage ScreenRenderer::render(const QString &wallpaperFile,
                              const QDateTime &now,
                              const QSize &size,
                              const QLocale &locale)
{
    if (!m_impl->valid || !size.isValid())
        return {};

    QQuickItem *root = m_impl->root.get();
    m_impl->window->setGeometry(0, 0, size.width(), size.height());
    root->setSize(size);
    root->setProperty("wallpaper", wallpaperFile.isEmpty() ? QUrl() : QUrl::fromLocalFile(wallpaperFile));
    root->setProperty("now", now);
    root->setProperty("locale", QVariant::fromValue(locale));

    // The wallpaper is decoded synchronously; if it did not load (corrupt or
    // unreadable file), do not show a half-drawn screen.
    if (!root->property("ready").toBool()) {
        qWarning() << "galleond: the wallpaper did not load" << wallpaperFile;
        return {};
    }

    // The software backend paints straight into a QImage, and only redraws the
    // parts that changed: the same image must be used from one frame to the next.
    if (m_impl->frame.size() != size) {
        m_impl->frame = QImage(size, QImage::Format_RGBA8888_Premultiplied);
        m_impl->frame.fill(Qt::black);
    }
    m_impl->window->setRenderTarget(QQuickRenderTarget::fromPaintDevice(&m_impl->frame));

    m_impl->control->polishItems();
    m_impl->control->sync();
    m_impl->control->render();
    return m_impl->frame.copy(); // a copy: the next frame paints over this image
}

} // namespace galleond
