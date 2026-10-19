// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "TileRenderer.h"

#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontMetrics>
#include <QIcon>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>

namespace galleond {

namespace {

constexpr int Size = galleon::protocol::KeyImageSize;
constexpr qreal CornerRadius = 18.0;
const QColor Background(0x1e, 0x22, 0x27);
const QColor BackgroundPressed(0x55, 0x5d, 0x68);
const QColor Foreground(0xf2, 0xf4, 0xf6);

bool hasImageExtension(const QString &name)
{
    const QString suffix = QFileInfo(name).suffix().toLower();
    return suffix == QLatin1String("png") || suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg")
           || suffix == QLatin1String("svg");
}

} // namespace

TileRenderer::TileRenderer(QString iconFolder)
    : m_iconFolder(std::move(iconFolder))
{
}

QImage TileRenderer::loadIcon(const QString &name, int size) const
{
    if (name.isEmpty())
        return {};

    // A file next to the pages file. The name has no slash (checked when the
    // file was read), and it is checked again here: this class is a boundary too.
    if (!m_iconFolder.isEmpty() && hasImageExtension(name) && !name.contains(QLatin1Char('/'))
        && !name.contains(QLatin1Char('\\'))) {
        const QFileInfo info(QDir(m_iconFolder).filePath(name));
        if (info.isFile() && !info.isSymLink() && info.size() > 0 && info.size() <= MaxIconFileBytes) {
            QImageReader reader(info.absoluteFilePath());
            reader.setDecideFormatFromContent(true);
            const QSize original = reader.size();
            if (original.isValid() && original.width() <= MaxIconDimension && original.height() <= MaxIconDimension) {
                reader.setScaledSize(original.scaled(size, size, Qt::KeepAspectRatio));
                const QImage image = reader.read();
                if (!image.isNull())
                    return image;
            }
        }
        return {}; // a file name that does not work as a file is not a theme name either
    }

    // The desktop's icon theme.
    const QIcon icon = QIcon::fromTheme(name);
    if (icon.isNull())
        return {};
    return icon.pixmap(size, size).toImage();
}

QImage TileRenderer::blank() const
{
    QImage image(Size, Size, QImage::Format_RGB32);
    image.fill(Qt::black);
    return image;
}

QImage TileRenderer::render(const KeyConfig &key, const QString &state, bool pressed) const
{
    QImage image(Size, Size, QImage::Format_RGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QPainterPath shape;
    shape.addRoundedRect(QRectF(3, 3, Size - 6, Size - 6), CornerRadius, CornerRadius);
    painter.fillPath(shape, pressed ? BackgroundPressed : Background);

    const bool hasLabel = !key.label.isEmpty();
    const int iconSize = hasLabel ? 84 : 112;
    const QImage icon = loadIcon(key.icon.forState(state), iconSize);

    // Icon in the upper part, label under it; a lone label is centered.
    const QRectF iconArea(0, hasLabel ? 14 : (Size - iconSize) / 2.0, Size, iconSize);
    if (!icon.isNull()) {
        const QSizeF fitted = QSizeF(icon.size()).scaled(iconArea.size(), Qt::KeepAspectRatio);
        const QRectF target(QPointF(iconArea.center().x() - fitted.width() / 2, iconArea.center().y() - fitted.height() / 2), fitted);
        painter.drawImage(target, icon);
    }

    if (hasLabel) {
        QFont font = painter.font();
        font.setPixelSize(icon.isNull() ? 30 : 22);
        font.setBold(true);
        painter.setFont(font);
        painter.setPen(Foreground);
        const QFontMetrics metrics(font);
        const QString text = metrics.elidedText(key.label, Qt::ElideRight, Size - 16);
        const QRectF labelArea(8, icon.isNull() ? 0 : Size - 46, Size - 16, icon.isNull() ? Size : 38);
        painter.drawText(labelArea, Qt::AlignCenter, text);
    }
    return image;
}

} // namespace galleond
