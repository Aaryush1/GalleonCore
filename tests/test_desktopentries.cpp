// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <DesktopEntries.h>

#include <QDir>
#include <QFile>
#include <QLocale>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>

using galleond::DesktopApp;
using galleond::DesktopEntries;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes, bool executable = false)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(bytes);
    file.close();
    if (executable)
        file.setPermissions(file.permissions() | QFileDevice::ExeOwner | QFileDevice::ExeUser);
    return true;
}

QByteArray entry(const QString &name, const QString &icon = {}, const QString &exec = QStringLiteral("/usr/bin/true %U"),
                 const QString &extra = {})
{
    QString text = "[Desktop Entry]\nType=Application\nName=" + name + "\nExec=" + exec + "\n";
    if (!icon.isEmpty())
        text += "Icon=" + icon + "\n";
    return (text + extra).toUtf8();
}

// Two data folders: the user's (XDG_DATA_HOME) and a system one (XDG_DATA_DIRS).
struct Environment
{
    QTemporaryDir home;
    QTemporaryDir system;
    QByteArray oldHome = qgetenv("XDG_DATA_HOME");
    QByteArray oldDirs = qgetenv("XDG_DATA_DIRS");
    QByteArray oldPath = qgetenv("PATH");

    Environment()
    {
        qputenv("XDG_DATA_HOME", home.path().toUtf8());
        qputenv("XDG_DATA_DIRS", system.path().toUtf8());
    }
    ~Environment()
    {
        qputenv("XDG_DATA_HOME", oldHome);
        qputenv("XDG_DATA_DIRS", oldDirs);
        qputenv("PATH", oldPath);
    }
    [[nodiscard]] QString userApp(const QString &file) const { return home.filePath("applications/" + file); }
    [[nodiscard]] QString systemApp(const QString &file) const { return system.filePath("applications/" + file); }
};

} // namespace

class TestDesktopEntries : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void referencesAreChecked_data()
    {
        QTest::addColumn<QString>("reference");
        QTest::addColumn<bool>("valid");
        QTest::newRow("id") << "firefox.desktop" << true;
        QTest::newRow("reverse domain id") << "org.kde.dolphin.desktop" << true;
        QTest::newRow("vendor dash id") << "kde-app-name.desktop" << true;
        QTest::newRow("absolute path") << "/usr/share/applications/vlc.desktop" << true;
        QTest::newRow("path with spaces") << "/home/me/My Apps/tool.desktop" << true;
        QTest::newRow("no suffix") << "firefox" << false;
        QTest::newRow("wrong suffix") << "firefox.sh" << false;
        QTest::newRow("relative path") << "applications/firefox.desktop" << false;
        QTest::newRow("dotdot") << "/usr/share/../../etc/x.desktop" << false;
        QTest::newRow("slash in id") << "a/b.desktop" << false;
        QTest::newRow("shell characters") << "a;rm -rf.desktop" << false;
        QTest::newRow("control character") << "/tmp/a\nb.desktop" << false;
        QTest::newRow("empty") << "" << false;
        QTest::newRow("only suffix") << ".desktop" << false; // no name before the suffix
        QTest::newRow("too long") << (QString(300, QLatin1Char('a')) + ".desktop") << false;
    }
    void referencesAreChecked()
    {
        QFETCH(QString, reference);
        QFETCH(bool, valid);
        QCOMPARE(DesktopEntries::isValidReference(reference), valid);
    }

    void anApplicationIsFoundByIdAndByPath()
    {
        Environment env;
        QVERIFY(writeFile(env.systemApp("tool.desktop"), entry("The Tool", "tool-icon")));
        const DesktopApp byId = DesktopEntries::lookup("tool.desktop");
        QVERIFY(byId.isValid());
        QCOMPARE(byId.name, QStringLiteral("The Tool"));
        QCOMPARE(byId.icon, QStringLiteral("tool-icon"));
        QCOMPARE(byId.id, QStringLiteral("tool.desktop"));

        const DesktopApp byPath = DesktopEntries::lookup(env.systemApp("tool.desktop"));
        QCOMPARE(byPath.file, byId.file);
        QVERIFY(!DesktopEntries::lookup("absent.desktop").isValid());
        QVERIFY(!DesktopEntries::lookup("/nonexistent/x.desktop").isValid());
        QVERIFY(!DesktopEntries::lookup("../x.desktop").isValid());
    }

    void theUsersFileHidesTheSystemOne()
    {
        Environment env;
        QVERIFY(writeFile(env.systemApp("tool.desktop"), entry("System Tool")));
        QVERIFY(writeFile(env.userApp("tool.desktop"), entry("My Tool")));
        QCOMPARE(DesktopEntries::lookup("tool.desktop").name, QStringLiteral("My Tool"));
    }

    void aVendorDashIdFindsASubFolder()
    {
        Environment env;
        QVERIFY(writeFile(env.systemApp("kde/app.desktop"), entry("KDE App")));
        QCOMPARE(DesktopEntries::lookup("kde-app.desktop").name, QStringLiteral("KDE App"));
    }

    void onlyRealApplicationsCount()
    {
        Environment env;
        QVERIFY(writeFile(env.systemApp("link.desktop"), "[Desktop Entry]\nType=Link\nName=A Link\nURL=https://x.org\nExec=true\n"));
        QVERIFY(writeFile(env.systemApp("noexec.desktop"), "[Desktop Entry]\nType=Application\nName=No Exec\n"));
        QVERIFY(writeFile(env.systemApp("empty.desktop"), ""));
        QVERIFY(writeFile(env.systemApp("junk.desktop"), "this is not a desktop file at all"));
        QVERIFY(writeFile(env.systemApp("huge.desktop"), entry("Huge") + QByteArray(70 * 1024, '#')));
        for (const QString &name : {QStringLiteral("link"), QStringLiteral("noexec"), QStringLiteral("empty"), QStringLiteral("junk"), QStringLiteral("huge")})
            QVERIFY2(!DesktopEntries::lookup(name + ".desktop").isValid(), qPrintable(name));
    }

    void anIconPathIsNotUsedAsAnIconName()
    {
        Environment env;
        QVERIFY(writeFile(env.systemApp("path.desktop"), entry("Path Icon", "/usr/share/pixmaps/x.png")));
        const DesktopApp app = DesktopEntries::lookup("path.desktop");
        QVERIFY(app.isValid());
        QVERIFY(app.icon.isEmpty());
    }

    void theNameIsInTheUsersLanguage()
    {
        Environment env;
        QVERIFY(writeFile(env.systemApp("l.desktop"), entry("Files", "f", "/usr/bin/true", "Name[fr]=Fichiers\nName[de]=Dateien\n")));
        const QLocale before = QLocale();
        QLocale::setDefault(QLocale(QLocale::French, QLocale::France));
        QCOMPARE(DesktopEntries::lookup("l.desktop").name, QStringLiteral("Fichiers"));
        QLocale::setDefault(QLocale(QLocale::Spanish, QLocale::Spain));
        QCOMPARE(DesktopEntries::lookup("l.desktop").name, QStringLiteral("Files")); // not translated: the default name
        QLocale::setDefault(before);
    }

    void theListSkipsHiddenAppsAndIsSorted()
    {
        Environment env;
        QVERIFY(writeFile(env.systemApp("b.desktop"), entry("Banana")));
        QVERIFY(writeFile(env.systemApp("a.desktop"), entry("apple")));
        QVERIFY(writeFile(env.systemApp("h.desktop"), entry("Hidden One", {}, "/usr/bin/true", "Hidden=true\n")));
        QVERIFY(writeFile(env.systemApp("n.desktop"), entry("No Display", {}, "/usr/bin/true", "NoDisplay=true\n")));
        QVERIFY(writeFile(env.systemApp("junk.desktop"), "x"));
        QVERIFY(writeFile(env.userApp("b.desktop"), entry("Cherry"))); // shadows the system Banana

        const QList<DesktopApp> apps = DesktopEntries::list();
        QStringList names;
        for (const DesktopApp &app : apps)
            names << app.name;
        QCOMPARE(names, (QStringList{"apple", "Cherry"}));
    }

    void anApplicationIsStartedWithOneArgumentAndNoShell()
    {
        Environment env;
        QTemporaryDir bin;
        const QString log = bin.filePath("calls.log");
        // A stand-in for gio: it records every argument on its own line.
        QVERIFY(writeFile(bin.filePath("gio"),
                          ("#!/bin/sh\nfor a in \"$@\"; do echo \"[$a]\" >> '" + log.toUtf8() + "'; done\n"), true));
        qputenv("PATH", bin.path().toUtf8());

        const QString name = "Odd name; touch pwned $(id) 'x'.desktop";
        QVERIFY(writeFile(env.systemApp(name), entry("Odd")));
        const DesktopApp app = DesktopEntries::lookup(env.systemApp(name));
        QVERIFY(app.isValid());
        QVERIFY(DesktopEntries::launch(app));

        QFile file(log);
        QTRY_VERIFY_WITH_TIMEOUT(file.exists() && file.size() > 0, 5000);
        QThread::msleep(100);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QStringList lines = QString::fromUtf8(file.readAll()).split('\n', Qt::SkipEmptyParts);
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines.at(0), QStringLiteral("[launch]"));
        QCOMPARE(lines.at(1), "[" + app.file + "]"); // ONE argument, the name untouched
        QVERIFY(!QFile::exists("pwned"));
    }

    void withoutAnyLauncherNothingStarts()
    {
        Environment env;
        QTemporaryDir empty;
        qputenv("PATH", empty.path().toUtf8());
        QVERIFY(writeFile(env.systemApp("t.desktop"), entry("T")));
        QVERIFY(!DesktopEntries::launch(DesktopEntries::lookup("t.desktop")));
        QVERIFY(!DesktopEntries::launch(DesktopApp{})); // an invalid app is never started
    }
};

QTEST_MAIN(TestDesktopEntries)
#include "test_desktopentries.moc"
