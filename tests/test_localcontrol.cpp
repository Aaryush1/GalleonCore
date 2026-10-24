// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LocalControl.h"
#include <QtTest>
#include <QLocalSocket>
#include <QUuid>
#include <memory>

using namespace galleond;
class TestLocalControl : public QObject
{
    Q_OBJECT
private slots:
    void refusesUnexpectedCommands()
    {
        for (const QByteArray &frame : {QByteArray{}, QByteArray("[]"), QByteArray(R"({"action":"exec"})"),
                 QByteArray(R"({"action":"stop","extra":true})"), QByteArray(R"({"action":"wallpaper","name":1})"),
                 QByteArray(R"({"action":"wallpaper","name":""})"), QByteArray(4097, 'x')})
            QVERIFY(!parseLocalCommand(frame).has_value());
    }
    void gamingCommandSelectsOnlyTheGamingPage()
    {
        const auto command = parseLocalCommand(encodeLocalCommand({LocalAction::Gaming, {}}));
        QVERIFY(command.has_value());
        QCOMPARE(command->action, LocalAction::Gaming);
        QVERIFY(!parseLocalCommand(R"({"action":"gaming","page":"arbitrary"})"));
        const QString name = QStringLiteral("galleon-gaming-") + QUuid::createUuid().toString(QUuid::Id128);
        LocalControl server; QVERIFY(server.listen(name));
        const QSignalSpy requested(&server, &LocalControl::gamingRequested);
        sendLocalCommand(name, {LocalAction::Gaming, {}}, &server);
        QTRY_COMPARE(requested.size(), 1);
    }
    void roundTripWallpaper()
    {
        const QString name = QString::fromUtf8("personal/caf\xc3\xa9.jpg");
        const auto command = parseLocalCommand(encodeLocalCommand({LocalAction::Wallpaper, name}));
        QVERIFY(command.has_value());
        QCOMPARE(command.value_or(LocalCommand{}).wallpaper, name);
        QCOMPARE(command.value_or(LocalCommand{}).action, LocalAction::Wallpaper);
    }
    void appliesOneCommandAndLeavesSecondInstanceAlone()
    {
        const QString name = QStringLiteral("galleon-test-") + QUuid::createUuid().toString(QUuid::Id128);
        LocalControl server;
        QVERIFY(server.listen(name));
        LocalControl second;
        QVERIFY(!second.listen(name));
        const auto requests = std::make_unique<QSignalSpy>(&server, &LocalControl::wallpaperRequested);
        sendLocalCommand(name, {LocalAction::Wallpaper, QStringLiteral("personal/example.jpg")}, &server);
        QTRY_COMPARE(requests->size(), 1);
        QCOMPARE(requests->first().first().toString(), QStringLiteral("personal/example.jpg"));
    }
    void invalidInputCannotStopDaemon()
    {
        const QString name = QStringLiteral("galleon-test-") + QUuid::createUuid().toString(QUuid::Id128);
        LocalControl server;
        QVERIFY(server.listen(name));
        const auto stops = std::make_unique<QSignalSpy>(&server, &LocalControl::stopRequested);
        QLocalSocket client;
        client.connectToServer(name);
        QVERIFY(client.waitForConnected());
        client.write("{\"action\":\"stop\",\"extra\":true}\n");
        QTRY_COMPARE(client.state(), QLocalSocket::UnconnectedState);
        QCOMPARE(stops->size(), 0);
    }
};
QTEST_GUILESS_MAIN(TestLocalControl)
#include "test_localcontrol.moc"
