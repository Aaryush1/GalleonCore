// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include <RecordingController.h>
#include <QTest>
#include <QTemporaryDir>
#include <QWebSocketServer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QCryptographicHash>
#include <memory>
using namespace galleond;
class RecordingTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void automaticUsesPlatformAndPersistsWithoutFallback() {
        const QTemporaryDir dir;
        const auto file = dir.filePath("recording.ini");
        RecordingController control(nullptr, file);
        control.setPlatformForTest(false, false, true, {});
        QCOMPARE(control.backend(), QString("obs"));
        QVERIFY(!control.options().at(1).toMap().value("available").toBool());
        QVERIFY(!control.configure("gamebar", 4455));
        control.setPlatformForTest(true, true, true, [](int) { return true; });
        QCOMPARE(control.backend(), QString("gamebar"));
        QVERIFY(control.configure("obs", 4545, "password", true));
        const RecordingController other(nullptr, file);
        QCOMPARE(other.choice(), QString("obs"));
        QCOMPARE(other.port(), 4545);
        QVERIFY(!control.configure("unknown", 4455));
        QVERIFY(!control.configure("auto", 0));
        control.setPlatformForTest(true, false, true, {});
        QVERIFY(control.configure("auto", 4455));
        QCOMPARE(control.backend(), QString("gamebar"));
        QVERIFY(!control.buttons().first().toMap().value("enabled").toBool());
    }
    void gameBarSendsOnlySupportedShortcutsAndNeverClaimsAnActiveState() {
        const QTemporaryDir dir; RecordingController control(nullptr, dir.filePath("recording.ini"));
        QList<int> sent;
        control.setPlatformForTest(true, true, false, [&sent](int index) { sent.append(index); return true; });
        for (int i=0; i<12; ++i) control.trigger(i);
        QCOMPARE(sent, (QList<int>{0,1,2,3}));
        for (const auto &button : control.buttons()) QVERIFY(!button.toMap().value("active").toBool());
        QVERIFY(control.status().contains("not confirmed"));
        control.setPlatformForTest(true, false, false, [&sent](int index) { sent.append(index); return true; });
        control.trigger(0); QCOMPARE(sent.size(), 4);
    }
    void runningPortableObsIsDiscoveredWithoutChangingGameBarDefault()
    {
        QWebSocketServer server("portable", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QTemporaryDir dir;
        RecordingController control(nullptr, dir.filePath("recording.ini"));
        control.setPlatformForTest(true, true, false, [](int) { return true; });
        QVERIFY(control.configure("auto", server.serverPort()));
        QTRY_VERIFY(server.hasPendingConnections());
        const std::unique_ptr<QWebSocket> peer(server.nextPendingConnection());
        QStringList requests;
        const auto send = [&peer](int op, const QJsonObject &data) { peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"op",op},{"d",data}}).toJson(QJsonDocument::Compact))); };
        connect(peer.get(), &QWebSocket::textMessageReceived, this, [&](const QString &text) {
            const auto root = QJsonDocument::fromJson(text.toUtf8()).object();
            const auto data = root.value("d").toObject();
            if (root.value("op").toInt() == 1) { send(2, {{"negotiatedRpcVersion",1}}); return; }
            requests.append(data.value("requestType").toString());
            send(7, {{"requestId",data.value("requestId")},{"requestStatus",QJsonObject{{"result",true},{"code",100}}},
                {"responseData",QJsonObject{{"availableRequests",QJsonArray{}}}}});
        });
        send(0, {{"rpcVersion",1}});
        QTRY_VERIFY(control.options().at(2).toMap().value("available").toBool());
        QCOMPARE(control.backend(), QString("gamebar"));
        QTRY_VERIFY(requests.contains("GetVersion"));
        QVERIFY(!requests.contains("ToggleRecord"));
        QVERIFY(!control.buttons().first().toMap().value("active").toBool());
    }
    void obsHandshakeTimeoutDisablesEveryAction()
    {
        QWebSocketServer server("silent", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QTemporaryDir dir;
        RecordingController control(nullptr, dir.filePath("recording.ini"));
        control.setPlatformForTest(false, false, true, {});
        QVERIFY(control.configure("obs", server.serverPort()));
        QTRY_VERIFY(server.hasPendingConnections());
        const std::unique_ptr<QWebSocket> peer(server.nextPendingConnection());
        QTRY_VERIFY_WITH_TIMEOUT(control.status().contains("timed out"), 5500);
        for (const auto &button : control.buttons()) {
            QVERIFY(!button.toMap().value("active").toBool());
            QVERIFY(!button.toMap().value("enabled").toBool());
        }
    }
    void obsAuthenticatesDiscoversCapabilitiesAndWaitsForConfirmedState() {
        QWebSocketServer server("test", QWebSocketServer::NonSecureMode);
        QVERIFY(server.listen(QHostAddress::LocalHost, 0));
        const QTemporaryDir dir;
        RecordingController control(nullptr, dir.filePath("recording.ini"));
        control.setPlatformForTest(false, false, true, {});
        QVERIFY(control.configure("obs", server.serverPort(), "secret", true));
        QTRY_VERIFY(server.hasPendingConnections());
        std::unique_ptr<QWebSocket> peer(server.nextPendingConnection());
        bool authenticated = false;
        bool recording = false;
        bool allowStatus = true;
        bool rejectFirst = true;
        QStringList commands;
        QString pendingStatus;
        const auto send = [&peer](int op, const QJsonObject &data) { peer->sendTextMessage(QString::fromUtf8(QJsonDocument(QJsonObject{{"op",op},{"d",data}}).toJson(QJsonDocument::Compact))); };
        connect(peer.get(), &QWebSocket::textMessageReceived, this, [&](const QString &text) {
            const auto root = QJsonDocument::fromJson(text.toUtf8()).object();
            const auto data = root.value("d").toObject();
            if (root.value("op").toInt() == 1) {
                auto hash = [](const QByteArray &value) { return QCryptographicHash::hash(value,QCryptographicHash::Sha256).toBase64(); };
                authenticated = data.value("authentication").toString().toLatin1() == hash(hash("secretsalt") + "challenge");
                send(2, {{"negotiatedRpcVersion",1}}); return;
            }
            const auto type = data.value("requestType").toString();
            commands.append(type);
            QJsonObject response;
            if (type == "GetVersion") response.insert("availableRequests", QJsonArray{"GetRecordStatus","GetReplayBufferStatus","ToggleRecord","SaveReplayBuffer","ToggleReplayBuffer"});
            else if (type == "GetRecordStatus") { if (!allowStatus) { pendingStatus = data.value("requestId").toString(); return; } response = {{"outputActive",recording},{"outputPaused",false}}; }
            else if (type == "GetReplayBufferStatus") response = {{"outputActive",false}};
            else if (type == "ToggleRecord") {
                if (rejectFirst) {
                    rejectFirst = false;
                    send(7, {{"requestId",data.value("requestId")},{"requestStatus",QJsonObject{{"result",false},{"code",500}}}});
                    return;
                }
                recording = true; allowStatus = false;
            }
            send(7, {{"requestId",data.value("requestId")},{"requestStatus",QJsonObject{{"result",true},{"code",100}}},{"responseData",response}});
        });
        send(0, {{"rpcVersion",1},{"authentication",QJsonObject{{"salt","salt"},{"challenge","challenge"}}}});
        QTRY_VERIFY(authenticated);
        QTRY_VERIFY(control.buttons().first().toMap().value("enabled").toBool());
        QVERIFY(!control.buttons().at(1).toMap().value("enabled").toBool()); // Unsupported pause request.
        QVERIFY(!control.buttons().at(2).toMap().value("enabled").toBool()); // Replay buffer stopped.
        control.trigger(0);
        QTRY_VERIFY(control.status().contains("500"));
        QVERIFY(!control.buttons().first().toMap().value("active").toBool());
        control.trigger(0);
        QTRY_VERIFY(recording);
        QVERIFY(commands.contains("ToggleRecord"));
        QVERIFY(!control.buttons().first().toMap().value("active").toBool()); // No speculative state after success.
        QTRY_VERIFY(!pendingStatus.isEmpty());
        send(7, {{"requestId",pendingStatus},{"requestStatus",QJsonObject{{"result",true},{"code",100}}},{"responseData",QJsonObject{{"outputActive",true},{"outputPaused",false}}}});
        QTRY_VERIFY(control.buttons().first().toMap().value("active").toBool());
        QVERIFY(control.buttons().first().toMap().value("label").toString().contains("Stop"));
        QVERIFY(!control.buttons().at(1).toMap().value("enabled").toBool()); // Missing capability even while recording.
        allowStatus = true;
        peer->close();
        QTRY_VERIFY(!control.buttons().first().toMap().value("enabled").toBool());
        QVERIFY(!control.buttons().first().toMap().value("active").toBool());
    }
};
QTEST_GUILESS_MAIN(RecordingTest)
#include "test_recording.moc"
