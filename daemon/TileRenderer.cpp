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

#include <algorithm>
#include <cmath>

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

KeyConfig TileRenderer::effectiveKey(const KeyConfig &key, const DesktopApp &app, QString &iconFile)
{
    KeyConfig effective = key;
    iconFile.clear();
    if (key.action.type == ActionType::App && key.label.isEmpty() && key.icon.name.isEmpty() && key.icon.byState.isEmpty()) {
        // The name of the file when the application was not found, so that the key says what is wrong.
        effective.label = (app.isValid() ? app.name : key.action.desktop.section(QLatin1Char('.'), 0, -2)).left(MaxLabelLength);
        effective.icon.name = app.icon;
        iconFile = app.iconPath;
    }
    return effective;
}

QImage TileRenderer::loadIconFile(const QString &path, int size)
{
    const QFileInfo info(path);
    if (!info.isFile() || info.size() <= 0 || info.size() > MaxIconFileBytes || !hasImageExtension(info.fileName()))
        return {};
    QImageReader reader(info.absoluteFilePath());
    reader.setDecideFormatFromContent(true);
    const QSize original = reader.size();
    if (original.isValid()) {
        if (original.width() > MaxIconDimension || original.height() > MaxIconDimension)
            return {};
        reader.setScaledSize(original.scaled(size, size, Qt::KeepAspectRatio));
    } else {
        reader.setScaledSize(QSize(size, size)); // a vector image has no size of its own
    }
    return reader.read();
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

QColor TileRenderer::tintBackground(std::uint32_t rgb)
{
    const QColor colour = QColor::fromRgb(rgb);
    // Same hue and saturation, a third of the brightness (but never pitch black,
    // so that the theme can still be told apart): the icons and labels stay readable.
    const float hue = colour.hsvHueF() < 0 ? 0.0F : static_cast<float>(colour.hsvHueF());
    return QColor::fromHsvF(hue, static_cast<float>(colour.hsvSaturationF()),
                            std::max(0.22F, static_cast<float>(colour.valueF()) * 0.35F));
}

QImage TileRenderer::blank(qint64 tint) const
{
    QImage image(Size, Size, QImage::Format_RGB32);
    image.fill(Qt::black);
    if (tint >= 0) {
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing);
        QPainterPath shape;
        shape.addRoundedRect(QRectF(3, 3, Size - 6, Size - 6), CornerRadius, CornerRadius);
        painter.fillPath(shape, tintBackground(static_cast<std::uint32_t>(tint)).darker(150));
    }
    return image;
}

QImage TileRenderer::render(const KeyConfig &key, const QString &state, bool pressed, const QString &fallbackIconFile,
                            qint64 tint, bool highlight) const
{
    QImage image(Size, Size, QImage::Format_RGB32);
    image.fill(Qt::black);
    QPainter painter(&image);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    QPainterPath shape;
    shape.addRoundedRect(QRectF(3, 3, Size - 6, Size - 6), CornerRadius, CornerRadius);
    QColor background = pressed ? BackgroundPressed : Background;
    if (tint >= 0) {
        background = tintBackground(static_cast<std::uint32_t>(tint));
        if (pressed)
            background = background.lighter(220);
    }
    painter.fillPath(shape, background);

    const bool hasLabel = !key.label.isEmpty();
    const int iconSize = hasLabel ? 84 : 112;
    QImage icon = loadIcon(key.icon.forState(state), iconSize);
    if (icon.isNull() && !fallbackIconFile.isEmpty())
        icon = loadIconFile(fallbackIconFile, iconSize);

    // Icon in the upper part, label under it; a lone label is centered.
    const QRectF iconArea(0, hasLabel ? 14 : (Size - iconSize) / 2.0, Size, iconSize);
    if (!icon.isNull()) {
        const QSizeF fitted = QSizeF(icon.size()).scaled(iconArea.size(), Qt::KeepAspectRatio);
        const QRectF target(QPointF(iconArea.center().x() - fitted.width() / 2, iconArea.center().y() - fitted.height() / 2), fitted);
        painter.drawImage(target, icon);
    }

    if (hasLabel) {
        QFont font = painter.font();
        font.setBold(true);
        painter.setPen(Foreground);
        if (icon.isNull()) {
            // A label alone fills the tile: a short one (a digit of the keypad) is drawn very large; a
            // longer one is wrapped on its words, in the largest size that fits.
            const QRectF area(10, 10, Size - 20, Size - 20);
            int pixelSize = key.label.size() <= 2 ? 72 : 34;
            for (; pixelSize > 16; pixelSize -= 2) {
                font.setPixelSize(pixelSize);
                const QRectF needed = QFontMetricsF(font).boundingRect(area, Qt::AlignCenter | Qt::TextWordWrap, key.label);
                if (needed.height() <= area.height() && needed.width() <= area.width() + 0.5)
                    break;
            }
            font.setPixelSize(pixelSize);
            painter.setFont(font);
            painter.drawText(area, Qt::AlignCenter | Qt::TextWordWrap, key.label);
        } else {
            font.setPixelSize(22);
            painter.setFont(font);
            const QFontMetrics metrics(font);
            const QString text = metrics.elidedText(key.label, Qt::ElideRight, Size - 16);
            painter.drawText(QRectF(8, Size - 46, Size - 16, 38), Qt::AlignCenter, text);
        }
    }
    if (highlight) {
        // A five-point star in the top right corner, drawn as a shape so that it needs no font.
        QPainterPath star;
        const QPointF center(Size - 26, 26);
        for (int i = 0; i < 10; ++i) {
            const qreal radius = i % 2 == 0 ? 15.0 : 6.5;
            const qreal angle = -M_PI / 2 + i * M_PI / 5;
            const QPointF point(center.x() + radius * std::cos(angle), center.y() + radius * std::sin(angle));
            if (i == 0)
                star.moveTo(point);
            else
                star.lineTo(point);
        }
        star.closeSubpath();
        painter.setPen(Qt::NoPen);
        painter.fillPath(star, QColor(0xff, 0xd5, 0x4f));
    }
    return image;
}

} // namespace galleond
