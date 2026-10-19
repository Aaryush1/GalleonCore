// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <TileRenderer.h>

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>

using namespace galleond;

namespace {

KeyConfig key(const QString &label, const QString &icon)
{
    KeyConfig config;
    config.key = 0;
    config.label = label;
    config.icon.name = icon;
    return config;
}

bool saveColour(const QString &path, QColor colour, int size = 64)
{
    QImage image(size, size, QImage::Format_RGB32);
    image.fill(colour);
    return image.save(path);
}

bool close(QRgb a, QRgb b, int tolerance = 12)
{
    return qAbs(qRed(a) - qRed(b)) <= tolerance && qAbs(qGreen(a) - qGreen(b)) <= tolerance
           && qAbs(qBlue(a) - qBlue(b)) <= tolerance;
}

} // namespace

class TestTileRenderer : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void tilesAreTheKeySize()
    {
        const TileRenderer renderer;
        QCOMPARE(renderer.render(key("Play", "missing"), {}, false).size(), QSize(160, 160));
        QCOMPARE(renderer.blank().size(), QSize(160, 160));
    }

    void aLabelIsDrawn()
    {
        const TileRenderer renderer;
        QVERIFY(renderer.render(key("Play", "missing"), {}, false) != renderer.render(key("", "missing"), {}, false));
        QVERIFY(renderer.render(key("A", "missing"), {}, false) != renderer.render(key("B", "missing"), {}, false));
    }

    void aLongLabelStaysInsideTheTile()
    {
        const TileRenderer renderer;
        const QImage tile = renderer.render(key(QString(24, QLatin1Char('W')), "missing"), {}, false);
        // The outer 3-pixel frame is never painted over, whatever the text.
        for (int i = 0; i < 160; ++i) {
            QCOMPARE(tile.pixel(i, 0), qRgb(0, 0, 0));
            QCOMPARE(tile.pixel(0, i), qRgb(0, 0, 0));
            QCOMPARE(tile.pixel(159, i), qRgb(0, 0, 0));
            QCOMPARE(tile.pixel(i, 159), qRgb(0, 0, 0));
        }
    }

    void pressedIsDifferent()
    {
        const TileRenderer renderer;
        const QImage normal = renderer.render(key("Play", "missing"), {}, false);
        const QImage pressed = renderer.render(key("Play", "missing"), {}, true);
        QVERIFY(normal != pressed);
        QVERIFY(qGray(pressed.pixel(10, 80)) > qGray(normal.pixel(10, 80)));
    }

    void anIconFileOfTheUserFolderIsUsed()
    {
        QTemporaryDir dir;
        QVERIFY(saveColour(dir.filePath("red.png"), Qt::red));
        const TileRenderer renderer(dir.path());
        const QImage tile = renderer.render(key("", "red.png"), {}, false);
        QVERIFY(close(tile.pixel(80, 80), qRgb(255, 0, 0)));
        // Without a label the icon is larger and centered; its corner is in the tile.
        QVERIFY(close(tile.pixel(40, 40), qRgb(255, 0, 0)));
    }

    void theIconDependsOnTheState()
    {
        QTemporaryDir dir;
        QVERIFY(saveColour(dir.filePath("play.png"), Qt::green));
        QVERIFY(saveColour(dir.filePath("pause.png"), Qt::blue));
        KeyConfig config = key("", "play.png");
        config.icon.byState.insert("playing", "pause.png");

        const TileRenderer renderer(dir.path());
        QVERIFY(close(renderer.render(config, "playing", false).pixel(80, 80), qRgb(0, 0, 255)));
        QVERIFY(close(renderer.render(config, "paused", false).pixel(80, 80), qRgb(0, 255, 0)));
        QVERIFY(close(renderer.render(config, {}, false).pixel(80, 80), qRgb(0, 255, 0)));
    }

    void iconsCannotLeaveTheFolder()
    {
        QTemporaryDir outer;
        QVERIFY(saveColour(outer.filePath("secret.png"), Qt::red));
        QDir().mkpath(outer.filePath("icons"));
        const TileRenderer renderer(outer.filePath("icons"));
        const QImage plain = renderer.render(key("", "none.png"), {}, false);
        for (const QString &name : {QStringLiteral("../secret.png"), QStringLiteral("/tmp/x.png"), QStringLiteral("..\\secret.png")})
            QCOMPARE(renderer.render(key("", name), {}, false), plain);
    }

    void symbolicLinksAndBadFilesAreIgnored()
    {
        QTemporaryDir outer;
        QTemporaryDir dir;
        QVERIFY(saveColour(outer.filePath("target.png"), Qt::red));
        QVERIFY(QFile::link(outer.filePath("target.png"), dir.filePath("link.png")));
        QFile bad(dir.filePath("bad.png"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("not an image");
        bad.close();
        QVERIFY(saveColour(dir.filePath("huge.png"), Qt::red, TileRenderer::MaxIconDimension + 1));

        const TileRenderer renderer(dir.path());
        const QImage plain = renderer.render(key("", "none.png"), {}, false);
        QCOMPARE(renderer.render(key("", "link.png"), {}, false), plain);
        QCOMPARE(renderer.render(key("", "bad.png"), {}, false), plain);
        QCOMPARE(renderer.render(key("", "huge.png"), {}, false), plain);
    }

    void aMissingIconStillGivesAUsableTile()
    {
        const TileRenderer renderer;
        QVERIFY(!renderer.render(key("Stop", "no-such-theme-icon"), {}, false).isNull());
        QVERIFY(!renderer.render(KeyConfig{}, {}, false).isNull());
    }
};

QTEST_MAIN(TestTileRenderer)
#include "test_tilerenderer.moc"
