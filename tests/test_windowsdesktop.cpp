// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DaemonStatus.h"
#include "DesktopEntries.h"
#include "LocalControl.h"
#include <QImage>
#include <QFile>
#include <QSettings>
#include <QTemporaryDir>
#include <QScopeGuard>
#include <QDir>
#include <QFileInfo>
#include <QtTest>
#include <QUuid>
#include <memory>

class TestWindowsDesktop : public QObject
{
    Q_OBJECT
private slots:
    void registeredApplicationWithoutShortcutIsListed()
    {
        if (qEnvironmentVariable("GITHUB_ACTIONS") != QStringLiteral("true"))
            QSKIP("Temporary App Paths registration is isolated to the Windows CI runner");
        QTemporaryDir dir;
        const QString executable = dir.filePath(QStringLiteral("chrome.exe"));
        QVERIFY(QFile::copy(QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("System32/notepad.exe")), executable));
        const QString group = QStringLiteral("galleon-ci-") + QUuid::createUuid().toString(QUuid::Id128) + QStringLiteral(".exe");
        QSettings registry(QStringLiteral("HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\App Paths"), QSettings::NativeFormat);
        const auto cleanup = qScopeGuard([&] { registry.remove(group); registry.sync(); });
        registry.setValue(group + QStringLiteral("/."), executable);
        registry.sync();
        const auto apps = galleond::DesktopEntries::list();
        bool found = false;
        for (const auto &app : apps) {
            if (app.file.compare(executable, Qt::CaseInsensitive) == 0) {
                found = true;
                QVERIFY(!app.iconPath.isEmpty());
                QVERIFY(app.categories.contains(QStringLiteral("AudioVideo")));
            }
        }
        QVERIFY(found);
    }

    void windowsExecutableHasANativeCachedIcon()
    {
        const QString file = QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("System32/notepad.exe"));
        const auto app = galleond::DesktopEntries::lookup(file);
        QVERIFY(app.isValid());
        QVERIFY(!app.iconPath.isEmpty());
        QVERIFY(!QImage(app.iconPath).isNull());
        QCOMPARE(galleond::DesktopEntries::lookup(file).iconPath, app.iconPath);
    }

    void statusAndStopUsePrivateEndpoint()
    {
        const QString name = QStringLiteral("galleon-test-") + QUuid::createUuid().toString(QUuid::Id128);
        auto server = std::make_unique<galleond::LocalControl>();
        QVERIFY(server->listen(name));
        const auto stops = std::make_unique<QSignalSpy>(server.get(), &galleond::LocalControl::stopRequested);
        bosun::DaemonStatus status({}, name, nullptr);
        QTRY_VERIFY(status.running());
        QVERIFY(status.stop().isEmpty());
        QTRY_COMPARE(stops->size(), 1);
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
QTEST_MAIN(TestWindowsDesktop)
#include "test_windowsdesktop.moc"
