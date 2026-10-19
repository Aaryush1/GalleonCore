// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "MprisController.h"
#include "PagesConfig.h"
#include "PagesController.h"
#include "ScreenController.h"
#include "ScreenRenderer.h"
#include "TileRenderer.h"
#include "WallpaperCatalog.h"

#include <galleon/Protocol.h>
#include <galleon/StreamDeck.h>

#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDateTime>
#include <QDebug>
#include <QImage>
#include <QPainter>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>

#include <memory>

namespace {

// A key picture for the hardware test: the key number on a colour that depends
// on it; `lit` is the white version shown while the key is held down.
QImage keyTestImage(int index, bool lit)
{
    constexpr int size = galleon::protocol::KeyImageSize;
    QImage image(size, size, QImage::Format_RGB32);
    image.fill(lit ? QColor(Qt::white) : QColor::fromHsv(index * 30, 200, 120));
    QPainter painter(&image);
    painter.setPen(lit ? QColor(Qt::black) : QColor(Qt::white));
    QFont font = painter.font();
    font.setPixelSize(size / 2);
    font.setBold(true);
    painter.setFont(font);
    painter.drawText(image.rect(), Qt::AlignCenter, QString::number(index));
    return image;
}

void drawKeyTest(galleon::StreamDeck &deck)
{
    for (int key = 0; key < galleon::protocol::KeyCount; ++key) {
        if (!deck.setKeyImage(key, keyTestImage(key, false)))
            qWarning() << "galleond: could not draw key" << key;
    }
}

// First of `candidates` that exists as a directory/file, or an empty string.
QString firstExisting(const QStringList &candidates, bool wantFile)
{
    for (const QString &path : candidates) {
        const QFileInfo info(path);
        if (!path.isEmpty() && (wantFile ? info.isFile() : info.isDir()))
            return info.absoluteFilePath();
    }
    return {};
}

QString defaultPagesFile(const QString &fromCommandLine)
{
    if (!fromCommandLine.isEmpty())
        return QFileInfo(fromCommandLine).absoluteFilePath();
    return QStandardPaths::writableLocation(QStandardPaths::ConfigLocation) + QStringLiteral("/galleoncore/pages.json");
}

// The 12 keys of the first page in one PNG (3 columns, 4 rows, like the module),
// for checking the icons without the hardware.
bool renderKeysTo(const QString &pagesFile, const QString &pngFile, const QString &state)
{
    QByteArray json = galleond::defaultPagesJson();
    QFile file(pagesFile);
    if (file.exists() && file.open(QIODevice::ReadOnly))
        json = file.read(galleond::MaxPagesFileBytes + 1);
    const galleond::PagesParseResult parsed = galleond::parsePagesConfig(json);
    if (!parsed.config) {
        for (const QString &line : parsed.errors)
            qCritical().noquote() << "galleond:" << line;
        return false;
    }
    const galleond::TileRenderer tiles(QFileInfo(pagesFile).absolutePath());
    constexpr int tile = galleon::protocol::KeyImageSize;
    constexpr int gap = 12;
    QImage sheet(3 * tile + 2 * gap, 4 * tile + 3 * gap, QImage::Format_RGB32);
    sheet.fill(QColor(0x10, 0x10, 0x10));
    QPainter painter(&sheet);
    const galleond::Page &page = parsed.config->pages.value(parsed.config->home);
    for (int key = 0; key < galleon::protocol::KeyCount; ++key) {
        const auto &configured = page.keys[static_cast<std::size_t>(key)];
        const QImage image = configured ? tiles.render(*configured, state, false) : tiles.blank();
        painter.drawImage((key % 3) * (tile + gap), (key / 3) * (tile + gap), image);
    }
    painter.end();
    return sheet.save(pngFile);
}

QString defaultWallpaperDir(const QString &fromCommandLine)
{
    QStringList candidates{fromCommandLine, qEnvironmentVariable("GALLEONCORE_WALLPAPERS")};
    for (const QString &base : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        candidates << base + QStringLiteral("/galleoncore/wallpaper");
#ifdef GALLEON_SOURCE_WALLPAPERS
    candidates << QStringLiteral(GALLEON_SOURCE_WALLPAPERS); // running from the build tree
#endif
    return firstExisting(candidates, false);
}

QString defaultWidgetFile(const QString &fromCommandLine)
{
    const QString name = QStringLiteral("/WallpaperClock.qml");
    QStringList candidates{fromCommandLine.isEmpty() ? QString() : fromCommandLine + name};
    if (!qEnvironmentVariable("GALLEONCORE_WIDGETS").isEmpty())
        candidates << qEnvironmentVariable("GALLEONCORE_WIDGETS") + name;
    for (const QString &base : QStandardPaths::standardLocations(QStandardPaths::GenericDataLocation))
        candidates << base + QStringLiteral("/galleoncore/widgets") + name;
#ifdef GALLEON_SOURCE_WIDGETS
    candidates << QStringLiteral(GALLEON_SOURCE_WIDGETS) + name;
#endif
    return firstExisting(candidates, true);
}

} // namespace

int main(int argc, char *argv[])
{
    const QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("galleond"));
    QGuiApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION_STRING));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("GalleonCore daemon for the Corsair Galleon 100 SD"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption previewOption(QStringLiteral("preview"),
                                           QStringLiteral("Open the window to pick the screen wallpaper."));
    const QCommandLineOption wallpapersOption(QStringLiteral("wallpapers"),
                                              QStringLiteral("Folder of wallpapers, one sub-folder per style."),
                                              QStringLiteral("dir"));
    const QCommandLineOption widgetsOption(QStringLiteral("widgets"),
                                           QStringLiteral("Folder holding WallpaperClock.qml."),
                                           QStringLiteral("dir"));
    const QCommandLineOption wallpaperOption(QStringLiteral("wallpaper"),
                                             QStringLiteral("Show this wallpaper now, e.g. manga-classic/manga-classic-01.jpg."),
                                             QStringLiteral("name"));
    const QCommandLineOption renderOption(QStringLiteral("render-to"),
                                          QStringLiteral("Draw the screen for --wallpaper into this PNG file and exit. Does not touch the hardware."),
                                          QStringLiteral("file"));
    const QCommandLineOption traceOption(QStringLiteral("trace-input"),
                                         QStringLiteral("Print the first bytes of every key and dial report the module sends."));
    const QCommandLineOption testKeysOption(QStringLiteral("test-keys"),
                                            QStringLiteral("Draw the key numbers on the 12 keys; a key turns white while pressed."));
    const QCommandLineOption pagesOption(QStringLiteral("pages"),
                                         QStringLiteral("Pages file (default: ~/.config/galleoncore/pages.json)."),
                                         QStringLiteral("file"));
    const QCommandLineOption noPagesOption(QStringLiteral("no-pages"),
                                           QStringLiteral("Do not put the button pages on the keys."));
    const QCommandLineOption renderKeysOption(QStringLiteral("render-keys"),
                                              QStringLiteral("Draw the keys of the first page into this PNG file and exit. Does not touch the hardware."),
                                              QStringLiteral("file"));
    const QCommandLineOption stateOption(QStringLiteral("state"),
                                         QStringLiteral("Player state shown by --render-keys: playing, paused or stopped."),
                                         QStringLiteral("state"), QStringLiteral("paused"));
    parser.addOptions({previewOption, wallpapersOption, widgetsOption, wallpaperOption, renderOption, traceOption, testKeysOption,
                       pagesOption, noPagesOption, renderKeysOption, stateOption});
    parser.process(app);

    const QString wallpaperDir = defaultWallpaperDir(parser.value(wallpapersOption));
    const QString widgetFile = defaultWidgetFile(parser.value(widgetsOption));
    if (widgetFile.isEmpty()) {
        qCritical() << "galleond: WallpaperClock.qml not found (see --widgets)";
        return 1;
    }
    if (wallpaperDir.isEmpty())
        qWarning() << "galleond: no wallpaper folder found (see --wallpapers)";

    if (parser.isSet(renderKeysOption)) {
        const bool ok = renderKeysTo(defaultPagesFile(parser.value(pagesOption)), parser.value(renderKeysOption),
                                     parser.value(stateOption));
        if (ok)
            qInfo() << "galleond: wrote" << parser.value(renderKeysOption);
        return ok ? 0 : 1;
    }

    if (parser.isSet(renderOption)) {
        // Offline mode: no deck, no D-Bus.
        const galleond::WallpaperCatalog catalog(wallpaperDir);
        const galleond::WallpaperEntry *entry = catalog.find(parser.value(wallpaperOption));
        if (!entry) {
            qCritical() << "galleond: --render-to needs --wallpaper with a name from the catalog";
            return 1;
        }
        galleond::ScreenRenderer offline(widgetFile);
        const QImage frame = offline.isValid()
            ? offline.render(entry->path, QDateTime::currentDateTime(),
                             QSize(galleon::protocol::ScreenWidth, galleon::protocol::ScreenHeight))
            : QImage();
        if (frame.isNull() || !frame.save(parser.value(renderOption))) {
            qCritical() << "galleond: could not draw or save the screen";
            return 1;
        }
        qInfo() << "galleond: wrote" << parser.value(renderOption);
        return 0;
    }

    galleon::StreamDeck deck;
    deck.setTraceInput(parser.isSet(traceOption));
    if (!deck.open())
        qWarning() << "galleond: no Galleon 100 SD found, waiting";
    else
        qInfo() << "galleond: Stream Deck module opened, serial" << deck.serialNumber();

    // Until keys and dials do something, say what they send: this is also how
    // the key numbering and the dial numbering are checked on the hardware.
    QObject::connect(&deck, &galleon::StreamDeck::keyPressed, [](int key) { qInfo() << "key pressed" << key; });
    QObject::connect(&deck, &galleon::StreamDeck::keyReleased, [](int key) { qInfo() << "key released" << key; });
    QObject::connect(&deck, &galleon::StreamDeck::dialRotated, [](int dial, int steps) {
        qInfo() << "dial" << dial << "rotated" << steps;
    });
    QObject::connect(&deck, &galleon::StreamDeck::dialPressed, [](int dial) { qInfo() << "dial" << dial << "pressed"; });
    QObject::connect(&deck, &galleon::StreamDeck::dialReleased, [](int dial) { qInfo() << "dial" << dial << "released"; });

    if (parser.isSet(testKeysOption) && deck.isOpen()) {
        drawKeyTest(deck);
        QObject::connect(&deck, &galleon::StreamDeck::keyPressed, &deck, [&deck](int key) {
            deck.setKeyImage(key, keyTestImage(key, true));
        });
        QObject::connect(&deck, &galleon::StreamDeck::keyReleased, &deck, [&deck](int key) {
            deck.setKeyImage(key, keyTestImage(key, false));
        });
    }

    galleond::ScreenRenderer renderer(widgetFile);
    if (!renderer.isValid())
        return 1;
    // The track screen sits next to the wallpaper widget (same folder).
    galleond::ScreenRenderer nowPlayingRenderer(QFileInfo(widgetFile).absolutePath() + QStringLiteral("/NowPlaying.qml"));
    std::unique_ptr<galleond::MprisController> media;
    const bool usePages = !parser.isSet(noPagesOption) && !parser.isSet(testKeysOption);
    if (usePages)
        media = std::make_unique<galleond::MprisController>();
    galleond::ScreenController controller(&deck, &renderer, galleond::WallpaperCatalog(wallpaperDir), widgetFile,
                                          media.get(), nowPlayingRenderer.isValid() ? &nowPlayingRenderer : nullptr);

    // The button pages: keys and dials drive the music player (MPRIS).
    std::unique_ptr<galleond::PagesController> pages;
    if (usePages) {
        const QString pagesFile = defaultPagesFile(parser.value(pagesOption));
        pages = std::make_unique<galleond::PagesController>(&deck, media.get(),
                                                            galleond::TileRenderer(QFileInfo(pagesFile).absolutePath()));
        QObject::connect(pages.get(), &galleond::PagesController::screenConfigChanged, &controller,
                         [&pages, &controller] { controller.setScreenConfig(pages->screenConfig()); });
        pages->loadFile(pagesFile);
        controller.setScreenConfig(pages->screenConfig());
        // A module plugged in again shows its old icons: send everything.
        QObject::connect(&controller, &galleond::ScreenController::deckAvailableChanged, pages.get(),
                         [&pages] { pages->redraw(true); });
    }

    // org.galleoncore.Deck on the session bus: SetWallpaper, ClearWallpaper, Wallpapers.
    auto bus = QDBusConnection::sessionBus();
    if (!bus.registerService(QStringLiteral("org.galleoncore.Deck"))
        || !bus.registerObject(QStringLiteral("/org/galleoncore/Deck"), &controller, QDBusConnection::ExportAllSlots)) {
        qWarning() << "galleond: cannot register on D-Bus (another galleond running?)";
    }

    if (parser.isSet(wallpaperOption)) {
        if (!controller.SetWallpaper(parser.value(wallpaperOption)))
            qWarning() << "galleond: could not show" << parser.value(wallpaperOption);
    } else {
        controller.restore();
    }

    QQmlApplicationEngine engine;
    if (parser.isSet(previewOption)) {
        engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
        engine.load(QUrl(QStringLiteral("qrc:/galleond/qml/Preview.qml")));
        if (engine.rootObjects().isEmpty())
            return 1;
    }

    return QGuiApplication::exec();
}
