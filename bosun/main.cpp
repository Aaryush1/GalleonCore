// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "BosunBackend.h"

#include <QCommandLineParser>
#include <QDebug>
#include <QDir>
#include <QGuiApplication>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QStandardPaths>
#include <QTimer>

#include <optional>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("galleon-bosun"));
    QGuiApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION_STRING));
    QGuiApplication::setApplicationDisplayName(QStringLiteral("Galleon Bosun"));

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
    parser.addOption(pagesOption);
    parser.addOption(tabOption);
    parser.addOption(keyOption);
    parser.addOption(shotOption);
    parser.process(app);

    const QString pagesFile = parser.isSet(pagesOption)
                                  ? QDir::current().absoluteFilePath(parser.value(pagesOption))
                                  : QStandardPaths::writableLocation(QStandardPaths::ConfigLocation)
                                        + QStringLiteral("/galleoncore/pages.json");
    const bosun::BosunBackend backend(pagesFile);

    QQmlApplicationEngine engine;
    engine.addImageProvider(QStringLiteral("tiles"), new bosun::TileProvider(&backend));
    engine.setInitialProperties({{QStringLiteral("backend"), QVariant::fromValue(&backend)}});
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
    if (const auto tab = number(tabOption))
        root->setProperty("currentTab", *tab);
    if (const auto key = number(keyOption))
        root->setProperty("selectedKey", *key);

    if (parser.isSet(shotOption)) {
        auto *window = qobject_cast<QQuickWindow *>(engine.rootObjects().constFirst());
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
