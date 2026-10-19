// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "ScreenController.h"
#include "ScreenRenderer.h"
#include "WallpaperCatalog.h"

#include <galleon/Protocol.h>
#include <galleon/StreamDeck.h>

#include <QCommandLineParser>
#include <QDBusConnection>
#include <QDateTime>
#include <QDebug>
#include <QImage>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStandardPaths>

namespace {

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
    parser.addOptions({previewOption, wallpapersOption, widgetsOption, wallpaperOption, renderOption});
    parser.process(app);

    const QString wallpaperDir = defaultWallpaperDir(parser.value(wallpapersOption));
    const QString widgetFile = defaultWidgetFile(parser.value(widgetsOption));
    if (widgetFile.isEmpty()) {
        qCritical() << "galleond: WallpaperClock.qml not found (see --widgets)";
        return 1;
    }
    if (wallpaperDir.isEmpty())
        qWarning() << "galleond: no wallpaper folder found (see --wallpapers)";

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

    galleond::ScreenRenderer renderer(widgetFile);
    if (!renderer.isValid())
        return 1;
    galleond::ScreenController controller(&deck, &renderer, galleond::WallpaperCatalog(wallpaperDir), widgetFile);

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
