// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "RecordingIcons.h"
#include <QPainter>
#include <QStringList>
#include <QPainterPath>
#include <QPolygonF>
namespace galleond {
QImage recordingIcon(const QString &name, int size) {
    const QStringList supported{QStringLiteral("record"),QStringLiteral("stop"),QStringLiteral("pause"),QStringLiteral("resume"),QStringLiteral("save-replay"),QStringLiteral("replay-start"),QStringLiteral("replay-stop"),QStringLiteral("screenshot"),QStringLiteral("gamebar")};
    if (size < 16 || size > 512 || !supported.contains(name)) return {};
    QImage image(size, size, QImage::Format_ARGB32_Premultiplied); image.fill(Qt::transparent);
    QPainter p(&image); p.setRenderHint(QPainter::Antialiasing); p.scale(size / 128.0, size / 128.0);
    p.setPen(QPen(Qt::white, 7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::white);
    if (name == QStringLiteral("record")) { p.setPen(Qt::NoPen); p.setBrush(QColor(255,80,90)); p.drawEllipse(QRectF(28,28,72,72)); }
    else if (name == QStringLiteral("stop")) p.drawRoundedRect(QRectF(30,30,68,68),6,6);
    else if (name == QStringLiteral("pause")) { p.drawRoundedRect(QRectF(31,25,20,78),3,3); p.drawRoundedRect(QRectF(77,25,20,78),3,3); }
    else if (name == QStringLiteral("resume")) p.drawPolygon(QPolygonF{QPointF(38,22),QPointF(102,64),QPointF(38,106)});
    else if (name == QStringLiteral("screenshot")) { p.setBrush(Qt::NoBrush); p.drawRoundedRect(QRectF(14,35,100,72),10,10); p.drawEllipse(QRectF(44,48,40,40)); p.drawLine(40,34,47,22); p.drawLine(47,22,80,22); p.drawLine(80,22,87,34); }
    else if (name == QStringLiteral("gamebar")) { p.setBrush(Qt::NoBrush); p.drawRoundedRect(QRectF(12,28,104,72),14,14); p.drawLine(29,64,57,64); p.drawLine(43,50,43,78); p.drawEllipse(QPointF(86,54),4,4); p.drawEllipse(QPointF(98,74),4,4); }
    else if (name == QStringLiteral("save-replay")) { p.setBrush(Qt::NoBrush); p.drawLine(64,20,64,80); p.drawLine(42,60,64,82); p.drawLine(86,60,64,82); p.drawLine(24,84,24,105); p.drawLine(24,105,104,105); p.drawLine(104,105,104,84); }
    else { p.setBrush(Qt::NoBrush); p.drawArc(QRectF(18,18,92,92),35*16,285*16); p.drawLine(102,18,104,44); p.drawLine(104,44,79,41); p.setBrush(Qt::white);
        if (name == QStringLiteral("replay-stop")) p.drawRect(QRectF(48,48,32,32));
        else p.drawPolygon(QPolygonF{QPointF(50,43),QPointF(84,64),QPointF(50,85)});
    }
    return image;
}
}
