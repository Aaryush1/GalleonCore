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
        QTemporaryDir const dir;
        QVERIFY(saveColour(dir.filePath("red.png"), Qt::red));
        const TileRenderer renderer(dir.path());
        const QImage tile = renderer.render(key("", "red.png"), {}, false);
        QVERIFY(close(tile.pixel(80, 80), qRgb(255, 0, 0)));
        // Without a label the icon is larger and centered; its corner is in the tile.
        QVERIFY(close(tile.pixel(40, 40), qRgb(255, 0, 0)));
    }

    void theIconDependsOnTheState()
    {
        QTemporaryDir const dir;
        QVERIFY(saveColour(dir.filePath("play.png"), Qt::green));
        QVERIFY(saveColour(dir.filePath("pause.png"), Qt::blue));
        KeyConfig config = key("", "play.png");
        config.icon.byState.insert("playing", "pause.png");

        const TileRenderer renderer(dir.path());
        QVERIFY(close(renderer.render(config, "playing", false).pixel(80, 80), qRgb(0, 0, 255)));
        QVERIFY(close(renderer.render(config, "paused", false).pixel(80, 80), qRgb(0, 255, 0)));
        QVERIFY(close(renderer.render(config, {}, false).pixel(80, 80), qRgb(0, 255, 0)));
    }

    void anApplicationIconGivenByPathIsUsedAsAFallback()
    {
        QTemporaryDir const dir;
        QTemporaryDir const other;
        QVERIFY(saveColour(other.filePath("app.png"), QColor(200, 100, 0)));
        const QString link = dir.filePath("current.png"); // like /snap/firefox/current/...
        QVERIFY(QFile::link(other.filePath("app.png"), link));
        const TileRenderer renderer;

        // No icon name on the key: the file of the application.
        const QImage viaLink = renderer.render(key("", ""), {}, false, link);
        QVERIFY(close(viaLink.pixel(80, 80), qRgb(200, 100, 0)));
        // An icon name that works wins over the fallback.
        QVERIFY(saveColour(dir.filePath("mine.png"), Qt::blue));
        const TileRenderer withFolder(dir.path());
        QVERIFY(close(withFolder.render(key("", "mine.png"), {}, false, link).pixel(80, 80), qRgb(0, 0, 255)));
    }

    void anApplicationKeyTakesTheApplicationsLabelAndIcon()
    {
        galleond::KeyConfig plain;
        plain.action.type = galleond::ActionType::App;
        plain.action.desktop = "tool.desktop";
        galleond::DesktopApp app;
        app.file = "/x/tool.desktop";
        app.name = "The Tool";
        app.icon = "tool-icon";
        app.iconPath = "/x/icon.png";
        QString iconFile;

        const galleond::KeyConfig effective = TileRenderer::effectiveKey(plain, app, iconFile);
        QCOMPARE(effective.label, QStringLiteral("The Tool"));
        QCOMPARE(effective.icon.name, QStringLiteral("tool-icon"));
        QCOMPARE(iconFile, QStringLiteral("/x/icon.png"));

        // A label or an icon of the user's own is kept as it is.
        galleond::KeyConfig mine = plain;
        mine.label = "Mine";
        QCOMPARE(TileRenderer::effectiveKey(mine, app, iconFile).label, QStringLiteral("Mine"));
        QVERIFY(TileRenderer::effectiveKey(mine, app, iconFile).icon.name.isEmpty());
        QVERIFY(iconFile.isEmpty());

        // An application that was not found: the key says which file is wrong.
        QCOMPARE(TileRenderer::effectiveKey(plain, galleond::DesktopApp{}, iconFile).label, QStringLiteral("tool"));

        // Other actions are never touched.
        galleond::KeyConfig media;
        media.action.type = galleond::ActionType::Media;
        QVERIFY(TileRenderer::effectiveKey(media, app, iconFile).label.isEmpty());
    }

    void anIconFileMustStillBeASaneImage()
    {
        QTemporaryDir const dir;
        QFile bad(dir.filePath("bad.png"));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("not an image");
        bad.close();
        QVERIFY(saveColour(dir.filePath("huge.png"), Qt::red, TileRenderer::MaxIconDimension + 1));
        QVERIFY(saveColour(dir.filePath("ok.png"), Qt::green));
        QVERIFY(TileRenderer::loadIconFile(dir.filePath("bad.png"), 64).isNull());
        QVERIFY(TileRenderer::loadIconFile(dir.filePath("huge.png"), 64).isNull());
        QVERIFY(TileRenderer::loadIconFile(dir.filePath("missing.png"), 64).isNull());
        QVERIFY(TileRenderer::loadIconFile(dir.path(), 64).isNull());
        QVERIFY(TileRenderer::loadIconFile("/etc/passwd", 64).isNull()); // not an image extension
        QVERIFY(!TileRenderer::loadIconFile(dir.filePath("ok.png"), 64).isNull());
        QVERIFY(TileRenderer::loadIconFile(dir.filePath("ok.png"), 64).width() <= 64);
    }

    void iconsCannotLeaveTheFolder()
    {
        QTemporaryDir const outer;
        QVERIFY(saveColour(outer.filePath("secret.png"), Qt::red));
        QDir().mkpath(outer.filePath("icons"));
        const TileRenderer renderer(outer.filePath("icons"));
        const QImage plain = renderer.render(key("", "none.png"), {}, false);
        for (const QString &name : {QStringLiteral("../secret.png"), QStringLiteral("/tmp/x.png"), QStringLiteral("..\\secret.png")})
            QCOMPARE(renderer.render(key("", name), {}, false), plain);
    }

    void symbolicLinksAndBadFilesAreIgnored()
    {
        QTemporaryDir const outer;
        QTemporaryDir const dir;
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

    void aThemeTintsTheBackgroundWithADarkShadeOfItsColour()
    {
        QGuiApplication::platformName(); // the renderer needs no window, only the application object
        const TileRenderer renderer;
        const KeyConfig plain = key("", "");
        const QRgb normal = renderer.render(plain, "", false).pixel(20, 20);
        const QRgb red = renderer.render(plain, "", false, {}, 0xe53935).pixel(20, 20);
        QVERIFY(red != normal);
        QVERIFY(qRed(red) > qGreen(red) && qRed(red) > qBlue(red)); // still reddish
        QVERIFY(qRed(red) < 0x80);                                   // much darker than the theme colour
        const QRgb white = renderer.render(plain, "", false, {}, 0xffffff).pixel(20, 20);
        QVERIFY(qRed(white) == qGreen(white) && qGreen(white) == qBlue(white)); // grey for a white theme
        QVERIFY(qRed(white) < 0x80);
        const QRgb navy = renderer.render(plain, "", false, {}, 0x1a237e).pixel(20, 20);
        QVERIFY(qBlue(navy) > qRed(navy)); // the hue survives a dark colour
        // Held: lighter than at rest.
        const QRgb held = renderer.render(plain, "", true, {}, 0xe53935).pixel(20, 20);
        QVERIFY(qRed(held) > qRed(red));
        // An empty key takes the tint too; without a theme it stays black.
        QCOMPARE(renderer.blank().pixel(80, 80), qRgb(0, 0, 0));
        QVERIFY(renderer.blank(0xe53935).pixel(80, 80) != qRgb(0, 0, 0));
    }

    void aSingleDigitIsDrawnLarge()
    {
        const TileRenderer renderer;
        const auto lit = [](const QImage &image) {
            int count = 0;
            for (int y = 0; y < image.height(); ++y) {
                for (int x = 0; x < image.width(); ++x)
                    count += qGray(image.pixel(x, y)) > 200 ? 1 : 0;
            }
            return count;
        };
        QVERIFY(lit(renderer.render(key("7", ""), "", false)) > lit(renderer.render(key("Play", ""), "", false)) / 2);
        QVERIFY(lit(renderer.render(key("7", ""), "", false)) > 300);
    }
};

QTEST_MAIN(TestTileRenderer)
#include "test_tilerenderer.moc"
