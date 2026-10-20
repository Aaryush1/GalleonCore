// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BosunBackend.h"
#include "DaemonStatus.h"
#include "Tray.h"
#include "WallpaperLibrary.h"

#include <AppsBackend.h>

#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QApplication>
#include <QIcon>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>

#include <optional>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("galleon-bosun"));
    QGuiApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION_STRING));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Galleon Bosun"));
    // The desktop file name is what Wayland shows the window under (icon and name); the
    // built-in icon is for a run from the build tree, before anything is installed.
    QGuiApplication::setDesktopFileName(QStringLiteral("galleon-bosun"));
    QGuiApplication::setWindowIcon(QIcon::fromTheme(QStringLiteral("galleon-bosun"), QIcon(QStringLiteral(":/bosun/icons/galleon-bosun.svg"))));

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Galleon Bosun: settings for the Corsair Galleon 100 SD"));
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption pagesOption({QStringLiteral("p"), QStringLiteral("pages")},
                                         QStringLiteral("Pages file (default: the one of galleond)."), QStringLiteral("file"));
    const QCommandLineOption shotOption(QStringLiteral("screenshot"),
                                        QStringLiteral("Save a picture of the window to <png> and quit (for checking without a screen)."),
                                        QStringLiteral("png"));
    const QCommandLineOption tabOption(QStringLiteral("tab"), QStringLiteral("Open on this tab (0 is the first)."), QStringLiteral("n"));
    const QCommandLineOption keyOption(QStringLiteral("key"), QStringLiteral("Open with this key of the page selected (0 is the first)."),
                                       QStringLiteral("n"));
    const QCommandLineOption inspectOption(QStringLiteral("inspect"),
                                           QStringLiteral("Open the Screensavers tab with this video file chosen (to be added)."),
                                           QStringLiteral("file"));
    const QCommandLineOption heightOption(QStringLiteral("height"), QStringLiteral("Open with a window this high, in pixels."), QStringLiteral("n"));
    const QCommandLineOption cropOption(QStringLiteral("crop"), QStringLiteral("With --inspect: open the crop window on the video."));
    const QCommandLineOption pictureOption(QStringLiteral("inspect-picture"),
                                           QStringLiteral("Open the Wallpapers tab with this picture file chosen (to be added)."), QStringLiteral("file"));
    parser.addOption(pagesOption);
    parser.addOption(pictureOption);
    parser.addOption(cropOption);
    parser.addOption(heightOption);
    parser.addOption(inspectOption);
    parser.addOption(tabOption);
    parser.addOption(keyOption);
    parser.addOption(shotOption);
    parser.process(app);

    const QString pagesFile = parser.isSet(pagesOption)
                                  ? QDir::current().absoluteFilePath(parser.value(pagesOption))
                                  : QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                                        + QStringLiteral("/galleoncore/pages.json");
    bosun::BosunBackend backend(pagesFile);
    bosun::DaemonStatus daemon;
#ifdef GALLEON_SOURCE_WALLPAPERS
    bosun::WallpaperLibrary library(bosun::WallpaperLibrary::defaultPlaces(QStringLiteral(GALLEON_SOURCE_WALLPAPERS))); // and the source tree, when running from the build
#else
    bosun::WallpaperLibrary library(bosun::WallpaperLibrary::defaultPlaces());
#endif

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("tiles"), new bosun::TileProvider(&backend));
    engine.addImageProvider(QStringLiteral("themeicon"), new galleond::ThemeIconProvider);
    engine.addImageProvider(QStringLiteral("wallpapers"), new bosun::WallpaperThumbnails(&library));
    engine.addImageProvider(QStringLiteral("wallpaperpreview"), new bosun::WallpaperPreview(&library));
    engine.setInitialProperties({{QStringLiteral("backend"), QVariant::fromValue(&backend)},
                                 {QStringLiteral("daemon"), QVariant::fromValue(&daemon)},
                                 {QStringLiteral("library"), QVariant::fromValue(&library)}});
    engine.load(QUrl(QStringLiteral("qrc:/bosun/qml/Bosun.qml")));
    if (engine.rootObjects().isEmpty())
        return 1;

    // Where to open, for a picture or for coming back to the same place.
    QObject *root = engine.rootObjects().constFirst();
    const auto number = [&parser](const QCommandLineOption &option) -> std::optional<int> {
        bool ok = false;
        const int value = parser.value(option).toInt(&ok);
        return parser.isSet(option) && ok && value >= 0 ? std::optional<int>(value) : std::nullopt;
    };
    if (const auto height = number(heightOption))
        root->setProperty("height", *height);
    if (const auto tab = number(tabOption))
        root->setProperty("currentTab", *tab);
    if (const auto key = number(keyOption))
        root->setProperty("selectedKey", *key);

    if (parser.isSet(inspectOption)) {
        backend.inspect(QDir::current().absoluteFilePath(parser.value(inspectOption)));
        root->setProperty("currentTab", root->property("tabs").toList().size() - 1);
        root->setProperty("cropOpen", parser.isSet(cropOption));
    }

    if (parser.isSet(pictureOption)) {
        library.inspect(QDir::current().absoluteFilePath(parser.value(pictureOption)));
        root->setProperty("currentTab", root->property("tabs").toList().size() - 2); // the Wallpapers tab
    }

    // The icon in the notification area: closing the window hides it, a click shows it again, and
    // the menu can quit (and stop galleond too). Without a notification area it is an ordinary window.
    auto *window = qobject_cast<QQuickWindow *>(root);
    bosun::Tray tray(QGuiApplication::windowIcon());
    if (window && !parser.isSet(shotOption) && bosun::Tray::isAvailable()) {
        QApplication::setQuitOnLastWindowClosed(false);
        root->setProperty("hideOnClose", true);
        const auto refresh = [&] { tray.update(window->isVisible(), daemon.running()); };
        QObject::connect(window, &QWindow::visibleChanged, &tray, refresh);
        QObject::connect(&daemon, &bosun::DaemonStatus::changed, &tray, refresh);
        QObject::connect(&tray, &bosun::Tray::chosen, &app, [&](bosun::TrayEntry::Action action) {
            switch (action) {
            case bosun::TrayEntry::Action::ToggleWindow:
                if (window->isVisible()) {
                    window->hide();
                } else {
                    window->show();
                    window->raise();
                    window->requestActivate();
                }
                break;
            case bosun::TrayEntry::Action::StartDaemon:
                static_cast<void>(daemon.start());
                break;
            case bosun::TrayEntry::Action::QuitAndStop:
                static_cast<void>(daemon.stop());
                QApplication::quit();
                break;
            case bosun::TrayEntry::Action::Quit:
                QApplication::quit();
                break;
            }
        });
        refresh();
        tray.show();
    }

    if (parser.isSet(shotOption)) {
        const QString png = parser.value(shotOption);
        int result = 0;
        QTimer::singleShot(700, &app, [&] {
            if (!window || !window->grabWindow().save(png)) {
                qCritical().noquote() << "galleon-bosun: cannot save" << png;
                result = 1;
            }
            QGuiApplication::exit(result);
        });
    }
    return QGuiApplication::exec();
}
