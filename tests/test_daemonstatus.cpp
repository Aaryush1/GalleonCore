// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <DaemonStatus.h>

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using bosun::DaemonStatus;

namespace {

// A program in place of systemctl: it writes its arguments to a file.
QString fakeSystemctl(const QString &dir)
{
    QString script = dir + "/systemctl";
    QFile file(script);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    file.write(("#!/bin/sh\necho \"$@\" >> " + dir + "/asked\n").toUtf8());
    file.close();
    QFile::setPermissions(script, QFile::permissions(script) | QFile::ExeUser);
    return script;
}

QString asked(const QString &dir)
{
    QFile file(dir + "/asked");
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

} // namespace

class TestDaemonStatus : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void theCommandAsksTheServiceOfTheUserAndNothingElse()
    {
        QCOMPARE(DaemonStatus::userCommand("start"), (QStringList{"--user", "start", "galleond.service"}));
        QCOMPARE(DaemonStatus::userCommand("stop"), (QStringList{"--user", "stop", "galleond.service"}));
    }

    void itFollowsTheNameOnTheBus()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        QVERIFY(bus.isConnected());
        QVERIFY(!bus.interface()->isServiceRegistered(DaemonStatus::BusName)); // a private bus: nobody has it
        const DaemonStatus status("/nonexistent/systemctl");
        QVERIFY(!status.running());
        const QSignalSpy changed(&status, &DaemonStatus::changed);
        QVERIFY(bus.registerService(DaemonStatus::BusName));
        QTRY_VERIFY_WITH_TIMEOUT(status.running(), 3000);
        QCOMPARE(changed.size(), 1);
        QVERIFY(bus.unregisterService(DaemonStatus::BusName));
        QTRY_VERIFY_WITH_TIMEOUT(!status.running(), 3000);
        QCOMPARE(changed.size(), 2);
    }

    void startingAsksSystemdOnlyWhenItIsNotRunning()
    {
        const QTemporaryDir dir;
        DaemonStatus status(fakeSystemctl(dir.path()));
        QVERIFY(status.start().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(asked(dir.path()), QStringLiteral("--user start galleond.service\n"), 3000);
        QVERIFY(status.stop().isEmpty()); // not running: nothing to stop, nothing asked
        QTest::qWait(300);
        QCOMPARE(asked(dir.path()), QStringLiteral("--user start galleond.service\n"));
    }

    void stoppingAsksSystemdWhenItIsRunning()
    {
        const QTemporaryDir dir;
        QDBusConnection bus = QDBusConnection::sessionBus();
        QVERIFY(bus.registerService(DaemonStatus::BusName));
        DaemonStatus status(fakeSystemctl(dir.path()));
        QVERIFY(status.running());
        QVERIFY(status.start().isEmpty()); // already running: nothing asked
        QVERIFY(status.stop().isEmpty());
        QTRY_COMPARE_WITH_TIMEOUT(asked(dir.path()), QStringLiteral("--user stop galleond.service\n"), 3000);
        QVERIFY(bus.unregisterService(DaemonStatus::BusName));
    }

    void aMissingSystemctlIsSaid()
    {
        DaemonStatus status("/nonexistent/systemctl");
        QVERIFY(!status.start().isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestDaemonStatus)
#include "test_daemonstatus.moc"
