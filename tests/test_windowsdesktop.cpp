// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "DaemonStatus.h"
#include "DesktopEntries.h"
#include "AppsBackend.h"
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
#include <QStandardPaths>
#include <windows.h>
#include <shobjidl.h>
#include <wrl/client.h>

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

    void browserShortcutsKeepSeparateAppsProfilesAndIcons()
    {
        if (qEnvironmentVariable("GITHUB_ACTIONS") != QStringLiteral("true"))
            QSKIP("Temporary Start Menu shortcuts are isolated to the Windows CI runner");
        const QTemporaryDir files;
        const QString root = QStandardPaths::writableLocation(QStandardPaths::ApplicationsLocation);
        QVERIFY(QDir().mkpath(root));
        const QTemporaryDir menu(QDir(root).filePath(QStringLiteral("galleon-pwa-XXXXXX")));
        QVERIFY(files.isValid());
        QVERIFY(menu.isValid());
        const QString executable = files.filePath(QStringLiteral("chrome.exe"));
        QVERIFY(QFile::copy(QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("System32/notepad.exe")), executable));
        const QString icon = files.filePath(QStringLiteral("web-app.ico"));
        QImage picture(32, 32, QImage::Format_ARGB32);
        picture.fill(Qt::red);
        QVERIFY(picture.save(icon, "ICO"));
        const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        const auto cleanup = qScopeGuard([apartment] { if (SUCCEEDED(apartment)) CoUninitialize(); });
        const auto makeLink = [&](const QString &name, const QString &arguments, bool customIcon) {
            Microsoft::WRL::ComPtr<IShellLinkW> link;
            if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(link.GetAddressOf())))
                || FAILED(link->SetPath(QDir::toNativeSeparators(executable).toStdWString().c_str()))
                || FAILED(link->SetArguments(arguments.toStdWString().c_str())))
                return false;
            if (customIcon && FAILED(link->SetIconLocation(QDir::toNativeSeparators(icon).toStdWString().c_str(), 0)))
                return false;
            Microsoft::WRL::ComPtr<IPersistFile> persist;
            return SUCCEEDED(link.As(&persist))
                && SUCCEEDED(persist->Save(QDir::toNativeSeparators(menu.filePath(name + QStringLiteral(".lnk"))).toStdWString().c_str(), TRUE));
        };
        QVERIFY(makeLink(QStringLiteral("Chrome"), {}, false));
        QVERIFY(makeLink(QStringLiteral("Chrome duplicate"), {}, false));
        QVERIFY(makeLink(QStringLiteral("Music PWA"), QStringLiteral("--profile-directory=Default --app-id=aaa"), true));
        QVERIFY(makeLink(QStringLiteral("Video PWA"), QStringLiteral("--profile-directory=Default --app-id=bbb"), true));
        QVERIFY(makeLink(QStringLiteral("Music other profile"), QStringLiteral("--profile-directory=Profile1 --app-id=aaa"), true));
        int count = 0;
        int webApps = 0;
        for (const auto &app : galleond::DesktopEntries::list()) {
            if (!app.file.startsWith(menu.path() + QLatin1Char('/')))
                continue;
            ++count;
            QVERIFY(galleond::DesktopEntries::lookup(app.file).isValid());
            if (!app.name.startsWith(QStringLiteral("Chrome"))) {
                ++webApps;
                const QImage cached(app.iconPath);
                QVERIFY(!cached.isNull());
                QCOMPARE(cached.pixelColor(cached.width() / 2, cached.height() / 2), QColor(Qt::red));
            }
        }
        QCOMPARE(count, 4); // one browser, two app IDs, plus the same app in a different profile
        QCOMPARE(webApps, 3);
    }

    void windowsExecutableHasANativeCachedIcon()
    {
        const QString file = QDir(qEnvironmentVariable("SystemRoot")).filePath(QStringLiteral("System32/notepad.exe"));
        const auto app = galleond::DesktopEntries::lookup(file);
        QVERIFY(app.isValid());
        QVERIFY(!app.iconPath.isEmpty());
        QVERIFY(!QImage(app.iconPath).isNull());
        galleond::ThemeIconProvider provider;
        const QPixmap shown = provider.requestPixmap(app.iconPath, nullptr, QSize(32, 32));
        QImage placeholder(32, 32, QImage::Format_ARGB32);
        placeholder.fill(QColor(0x40, 0x46, 0x50));
        QVERIFY(shown.toImage() != placeholder);
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
