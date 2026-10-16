// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <galleon/StreamDeck.h>

#include <QDBusConnection>
#include <QDebug>
#include <QGuiApplication>

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("galleond"));
    QGuiApplication::setApplicationVersion(QStringLiteral(PROJECT_VERSION_STRING));

    galleon::StreamDeck deck;
    if (!deck.open())
        qWarning() << "galleond: no Galleon 100 SD found, waiting";
    else
        qInfo() << "galleond: Stream Deck module opened, serial" << deck.serialNumber();

    QObject::connect(&deck, &galleon::StreamDeck::keyPressed, [](int key) {
        qInfo() << "key pressed" << key;
    });

    // TODO: expose org.galleoncore.Deck on the session bus, render widgets/
    // off-screen and push frames to the deck.
    if (!QDBusConnection::sessionBus().registerService(QStringLiteral("org.galleoncore.Deck")))
        qWarning() << "galleond: cannot register D-Bus service";

    return app.exec();
}
