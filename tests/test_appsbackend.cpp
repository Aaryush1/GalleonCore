// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <AppsBackend.h>
#include <PagesConfig.h>

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using galleond::AppsBackend;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(bytes);
    return true;
}

struct Environment
{
    QTemporaryDir dir;
    QByteArray oldHome = qgetenv("XDG_DATA_HOME");
    QByteArray oldDirs = qgetenv("XDG_DATA_DIRS");
    Environment()
    {
        qputenv("XDG_DATA_HOME", dir.filePath("home").toUtf8());
        qputenv("XDG_DATA_DIRS", dir.filePath("system").toUtf8());
        for (const auto &[file, name] : {std::pair{"zeta.desktop", "Zeta"}, std::pair{"alpha.desktop", "alpha"}})
            if (!writeFile(dir.filePath(QString("system/applications/") + file),
                           QByteArray("[Desktop Entry]\nType=Application\nName=") + name + "\nExec=/usr/bin/true\nIcon=zicon\n"))
                qFatal("cannot write the desktop file");
    }
    ~Environment()
    {
        qputenv("XDG_DATA_HOME", oldHome);
        qputenv("XDG_DATA_DIRS", oldDirs);
    }
    [[nodiscard]] QString pages() const { return dir.filePath("config/pages.json"); }
};

QVariantMap key(const AppsBackend &backend, int index)
{
    return backend.keys().at(index).toMap();
}

} // namespace

class TestAppsBackend : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void applicationsAreListedByName()
    {
        Environment const env;
        AppsBackend const backend(env.pages());
        QCOMPARE(backend.apps().size(), 2);
        QCOMPARE(backend.apps().at(0).toMap().value("name").toString(), QStringLiteral("alpha"));
        QCOMPARE(backend.apps().at(1).toMap().value("id").toString(), QStringLiteral("zeta.desktop"));
        QCOMPARE(backend.apps().at(0).toMap().value("icon").toString(), QStringLiteral("zicon"));
    }

    void theKeysOfTheHomePageAreShown()
    {
        Environment const env; // no file yet: the built-in pages
        AppsBackend const backend(env.pages());
        QVERIFY(backend.problem().isEmpty());
        QCOMPARE(backend.page(), QStringLiteral("main"));
        QCOMPARE(backend.keys().size(), 12);
        QCOMPARE(key(backend, 0).value("kind").toString(), QStringLiteral("media"));
        QCOMPARE(key(backend, 0).value("label").toString(), QStringLiteral("Précédent"));
        QCOMPARE(key(backend, 4).value("kind").toString(), QStringLiteral("media"));
    }

    void assigningAnApplicationEditsTheFileAndRefreshesTheKeys()
    {
        Environment const env;
        AppsBackend backend(env.pages());
        QSignalSpy const changed(&backend, &AppsBackend::changed);
        QCOMPARE(backend.assign("zeta.desktop", 4), QString());
        QVERIFY(changed.count() >= 1);
        QCOMPARE(key(backend, 4).value("kind").toString(), QStringLiteral("app"));
        QCOMPARE(key(backend, 4).value("label").toString(), QStringLiteral("Zeta")); // the application's name
        QCOMPARE(key(backend, 4).value("icon").toString(), QStringLiteral("zicon"));
        QVERIFY(QFile::exists(env.pages()));

        QCOMPARE(backend.clear(4), QString());
        QCOMPARE(key(backend, 4).value("kind").toString(), QStringLiteral("empty"));
    }

    void aRefusedEditSaysWhy()
    {
        Environment const env;
        AppsBackend backend(env.pages());
        QVERIFY(!backend.assign("nothing.desktop", 4).isEmpty());
        QVERIFY(!backend.assign("zeta.desktop", 12).isEmpty());
        QVERIFY(!backend.clear(-1).isEmpty());
        QVERIFY(!QFile::exists(env.pages())); // nothing was written
    }

    void aFileWithErrorsIsReportedAndNotEdited()
    {
        Environment const env;
        const QByteArray broken = R"({ "version": 1, "home": "main", "pages": { "main": { "keys": [ { "key": 99 } ] } } })";
        QVERIFY(writeFile(env.pages(), broken));
        AppsBackend backend(env.pages());
        QVERIFY(backend.problem().contains("keys[0].key"));
        QVERIFY(backend.keys().isEmpty());
        QVERIFY(!backend.assign("zeta.desktop", 4).isEmpty());
        QFile file(env.pages());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), broken);
    }

    void severalPagesCanBeSwitched()
    {
        Environment const env;
        QVERIFY(writeFile(env.pages(), "{ \"version\": 1, \"home\": \"main\", \"pages\": { \"main\": { \"keys\": [] }, \"apps\": { \"keys\": [] } } }"));
        AppsBackend backend(env.pages());
        QCOMPARE(backend.pages(), (QStringList{"apps", "main"}));
        QCOMPARE(backend.page(), QStringLiteral("main"));
        backend.setPage("apps");
        QCOMPARE(backend.page(), QStringLiteral("apps"));
        QCOMPARE(backend.assign("alpha.desktop", 0), QString());
        QCOMPARE(key(backend, 0).value("kind").toString(), QStringLiteral("app"));
        backend.setPage("main");
        QCOMPARE(key(backend, 0).value("kind").toString(), QStringLiteral("empty")); // the other page is untouched
        backend.setPage("nowhere"); // ignored
        QCOMPARE(backend.page(), QStringLiteral("main"));
    }

    void anApplicationIconByPathReachesTheWindow()
    {
        Environment const env;
        QImage icon(64, 64, QImage::Format_RGB32);
        icon.fill(QColor(200, 100, 0));
        QVERIFY(icon.save(env.dir.filePath("snap-icon.png")));
        QVERIFY(writeFile(env.dir.filePath("system/applications/snappy.desktop"),
                          "[Desktop Entry]\nType=Application\nName=Snappy\nExec=/usr/bin/true\nIcon=" + env.dir.filePath("snap-icon.png").toUtf8() + "\n"));
        AppsBackend backend(env.pages());
        QCOMPARE(backend.assign("snappy.desktop", 2), QString());
        QCOMPARE(key(backend, 2).value("icon").toString(), env.dir.filePath("snap-icon.png")); // the path reaches the window

        galleond::ThemeIconProvider provider;
        QSize size;
        const QPixmap pixmap = provider.requestPixmap(env.dir.filePath("snap-icon.png"), &size, QSize(32, 32));
        QVERIFY(!pixmap.isNull());
        QCOMPARE(pixmap.toImage().pixelColor(16, 16), QColor(200, 100, 0));
    }

    void theIconProviderAlwaysAnswers()
    {
        galleond::ThemeIconProvider provider;
        QSize size;
        QVERIFY(!provider.requestPixmap("no-such-icon-anywhere", &size, QSize(48, 48)).isNull());
        QVERIFY(size.isValid());
        QVERIFY(!provider.requestPixmap("", &size, QSize()).isNull());
        QVERIFY(!provider.requestPixmap("../../etc/passwd", &size, QSize(100000, 100000)).isNull()); // size is capped
        QVERIFY(size.width() <= 256);
    }
};

QTEST_MAIN(TestAppsBackend)
#include "test_appsbackend.moc"
