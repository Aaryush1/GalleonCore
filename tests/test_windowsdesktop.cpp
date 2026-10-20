// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DaemonStatus.h"
#include "DesktopEntries.h"
#include "LocalControl.h"
#include <QtTest>
#include <QUuid>
#include <memory>

class TestWindowsDesktop : public QObject
{
    Q_OBJECT
private slots:
    void statusAndStopUsePrivateEndpoint()
    {
        const QString name = QStringLiteral("galleon-test-") + QUuid::createUuid().toString(QUuid::Id128);
        auto server = std::make_unique<galleond::LocalControl>();
        QVERIFY(server->listen(name));
        QSignalSpy stops(server.get(), &galleond::LocalControl::stopRequested);
        bosun::DaemonStatus status({}, name, nullptr);
        QTRY_VERIFY(status.running());
        QVERIFY(status.stop().isEmpty());
        QTRY_COMPARE(stops.size(), 1);
        server.reset();
        QTRY_VERIFY(!status.running());
    }
    void relativeExecutableCannotBeStarted()
    {
        const QString name = QStringLiteral("galleon-test-") + QUuid::createUuid().toString(QUuid::Id128);
        bosun::DaemonStatus status(QStringLiteral("untrusted-relative.exe"), name, nullptr);
        QVERIFY(!status.start().isEmpty());
    }
    void launcherRefusesCommandsAndUris()
    {
        QVERIFY(!galleond::DesktopEntries::isValidReference(QStringLiteral("cmd.exe /c whoami")));
        QVERIFY(!galleond::DesktopEntries::isValidReference(QStringLiteral("https://example.com/app.exe")));
        QVERIFY(!galleond::DesktopEntries::isValidReference(QStringLiteral("relative.lnk")));
        QVERIFY(!galleond::DesktopEntries::isValidReference(QStringLiteral("C:/example/script.cmd")));
        QVERIFY(galleond::DesktopEntries::isValidReference(QStringLiteral("C:/example/application.exe")));
        QVERIFY(galleond::DesktopEntries::isValidReference(QStringLiteral("C:/example/application.lnk")));
    }
};
QTEST_GUILESS_MAIN(TestWindowsDesktop)
#include "test_windowsdesktop.moc"
