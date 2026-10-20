// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <WallpaperCatalog.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

using galleond::WallpaperCatalog;

namespace {

bool writeImage(const QString &path, int width, int height, const char *format)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::darkCyan);
    return image.save(path, format);
}

} // namespace

class TestWallpaperCatalog : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void severalFoldersAreMergedAndTheFirstWinsOnAName()
    {
        QTemporaryDir const user;
        QTemporaryDir const shared;
        QVERIFY(writeImage(user.filePath("mine/m-01.jpg"), 32, 18, "JPEG"));
        QVERIFY(writeImage(user.filePath("common/c-01.jpg"), 40, 18, "JPEG"));  // the same name in both
        QVERIFY(writeImage(shared.filePath("common/c-01.jpg"), 20, 18, "JPEG"));
        QVERIFY(writeImage(shared.filePath("common/c-02.jpg"), 32, 18, "JPEG"));
        QVERIFY(writeImage(shared.filePath("zeta/z-01.png"), 32, 18, "PNG"));

        const WallpaperCatalog catalog(QStringList{user.path(), shared.path()});
        QStringList names;
        for (const auto &entry : catalog.entries())
            names << entry.name;
        // Sorted as one list, whichever folder a file is in.
        QCOMPARE(names, (QStringList{"common/c-01.jpg", "common/c-02.jpg", "mine/m-01.jpg", "zeta/z-01.png"}));
        QVERIFY(catalog.find("common/c-01.jpg")->path.startsWith(user.path())); // the first folder's file
        QVERIFY(catalog.find("common/c-02.jpg")->path.startsWith(shared.path()));
    }

    void aCatalogCanBeReadAgain()
    {
        QTemporaryDir const dir;
        QVERIFY(writeImage(dir.filePath("s/a-01.jpg"), 32, 18, "JPEG"));
        WallpaperCatalog catalog(dir.path());
        QCOMPARE(catalog.entries().size(), 1);
        QVERIFY(writeImage(dir.filePath("s/a-02.jpg"), 32, 18, "JPEG"));
        QVERIFY(writeImage(dir.filePath("new-style/n-01.png"), 32, 18, "PNG"));
        QCOMPARE(catalog.entries().size(), 1); // not before it is asked
        catalog.rescan();
        QCOMPARE(catalog.entries().size(), 3);
        QVERIFY(QFile::remove(dir.filePath("s/a-01.jpg")));
        catalog.rescan();
        QVERIFY(!catalog.find("s/a-01.jpg"));
        QCOMPARE(catalog.entries().size(), 2);
    }

    void theFoldersToReadAreTheOnesThatExistInOrderOnceEach()
    {
        QTemporaryDir const home;
        QTemporaryDir const flag;
        QTemporaryDir const tree;
        const QByteArray oldData = qgetenv("XDG_DATA_HOME");
        const QByteArray oldDirs = qgetenv("XDG_DATA_DIRS");
        const QByteArray oldEnv = qgetenv("GALLEONCORE_WALLPAPERS");
        qputenv("XDG_DATA_HOME", home.filePath("data").toUtf8());
        qputenv("XDG_DATA_DIRS", home.filePath("system").toUtf8());
        qunsetenv("GALLEONCORE_WALLPAPERS");
        QDir().mkpath(home.filePath("data/galleoncore/wallpaper"));
        QDir().mkpath(home.filePath("system/galleoncore/wallpaper"));
        QCOMPARE(WallpaperCatalog::userRoot(), home.filePath("data/galleoncore/wallpaper"));

        const QStringList roots = WallpaperCatalog::defaultRoots(flag.path(), tree.path());
        QCOMPARE(roots, (QStringList{flag.path(), home.filePath("data/galleoncore/wallpaper"), home.filePath("system/galleoncore/wallpaper"), tree.path()}));
        // A folder that is not there is left out, and one given twice counts once.
        QCOMPARE(WallpaperCatalog::defaultRoots("/nonexistent/x", flag.path()), (QStringList{home.filePath("data/galleoncore/wallpaper"), home.filePath("system/galleoncore/wallpaper"), flag.path()}));
        QCOMPARE(WallpaperCatalog::defaultRoots(tree.path(), tree.path()).count(tree.path()), 1);
        qputenv("GALLEONCORE_WALLPAPERS", flag.path().toUtf8());
        QCOMPARE(WallpaperCatalog::defaultRoots("", "").first(), flag.path());

        qputenv("XDG_DATA_HOME", oldData);
        qputenv("XDG_DATA_DIRS", oldDirs);
        if (oldEnv.isEmpty())
            qunsetenv("GALLEONCORE_WALLPAPERS");
        else
            qputenv("GALLEONCORE_WALLPAPERS", oldEnv);
    }

    void missingFolderGivesAnEmptyCatalog()
    {
        QVERIFY(WallpaperCatalog(QStringLiteral("/nonexistent/galleon")).entries().isEmpty());
        QVERIFY(WallpaperCatalog().entries().isEmpty());
    }

    void listsImagesByStyleInOrder()
    {
        QTemporaryDir const dir;
        QVERIFY(writeImage(dir.filePath("b-style/b-02.jpg"), 32, 18, "JPEG"));
        QVERIFY(writeImage(dir.filePath("b-style/b-01.jpg"), 32, 18, "JPEG"));
        QVERIFY(writeImage(dir.filePath("a-style/a-01.png"), 32, 18, "PNG"));

        const WallpaperCatalog catalog(dir.path());
        QCOMPARE(catalog.entries().size(), 3);
        QCOMPARE(catalog.entries()[0].name, QStringLiteral("a-style/a-01.png"));
        QCOMPARE(catalog.entries()[1].name, QStringLiteral("b-style/b-01.jpg"));
        QCOMPARE(catalog.entries()[2].name, QStringLiteral("b-style/b-02.jpg"));
        QCOMPARE(catalog.entries()[0].style, QStringLiteral("a-style"));
        QVERIFY(QFileInfo(catalog.entries()[0].path).isAbsolute());
    }

    void numbersAreReadAsNumbersAndCaseDoesNotMatter()
    {
        QTemporaryDir const dir;
        for (const char *name : {"x-10.jpg", "x-2.jpg", "x-1.jpg", "X-3.jpg"})
            QVERIFY(writeImage(dir.filePath(QStringLiteral("style/") + name), 32, 18, "JPEG"));
        QVERIFY(writeImage(dir.filePath("style-10/y-1.jpg"), 32, 18, "JPEG"));
        QVERIFY(writeImage(dir.filePath("style-2/y-1.jpg"), 32, 18, "JPEG"));

        const WallpaperCatalog catalog(dir.path());
        QStringList names;
        for (const auto &entry : catalog.entries())
            names << entry.name;
        QCOMPARE(names, (QStringList{"style/x-1.jpg", "style/x-2.jpg", "style/X-3.jpg", "style/x-10.jpg",
                                     "style-2/y-1.jpg", "style-10/y-1.jpg"}));
        // The same order again: it does not depend on the file system.
        QCOMPARE(WallpaperCatalog(dir.path()).entries().size(), catalog.entries().size());
    }

    void onlyListedNamesAreFound()
    {
        QTemporaryDir const dir;
        QVERIFY(writeImage(dir.filePath("s/one.jpg"), 32, 18, "JPEG"));
        const WallpaperCatalog catalog(dir.path());
        QVERIFY(catalog.find(QStringLiteral("s/one.jpg")) != nullptr);
        QVERIFY(catalog.find(QStringLiteral("s/two.jpg")) == nullptr);
        QVERIFY(catalog.find(QStringLiteral("../../etc/passwd")) == nullptr);
        QVERIFY(catalog.find(QStringLiteral("/etc/passwd")) == nullptr);
        QVERIFY(catalog.find(QString()) == nullptr);
    }

    void contentDecidesNotTheExtension()
    {
        QTemporaryDir const dir;
        QDir().mkpath(dir.filePath("s"));
        QFile fake(dir.filePath("s/fake.jpg"));
        QVERIFY(fake.open(QIODevice::WriteOnly));
        fake.write("this is not an image at all");
        fake.close();
        QFile truncated(dir.filePath("s/empty.png"));
        QVERIFY(truncated.open(QIODevice::WriteOnly));
        truncated.close();
        QVERIFY(writeImage(dir.filePath("s/real.png"), 16, 16, "PNG"));

        const WallpaperCatalog catalog(dir.path());
        QCOMPARE(catalog.entries().size(), 1);
        QCOMPARE(catalog.entries()[0].name, QStringLiteral("s/real.png"));
    }

    void otherExtensionsAreIgnored()
    {
        QTemporaryDir const dir;
        QVERIFY(writeImage(dir.filePath("s/pic.bmp"), 16, 16, "BMP"));
        QVERIFY(writeImage(dir.filePath("s/pic.jpg.txt"), 16, 16, "JPEG"));
        QVERIFY(WallpaperCatalog(dir.path()).entries().isEmpty());
    }

    void symbolicLinksAreIgnored()
    {
        QTemporaryDir const dir;
        QTemporaryDir const outside;
        QVERIFY(writeImage(outside.filePath("secret.jpg"), 16, 16, "JPEG"));
        QDir().mkpath(dir.filePath("s"));
        QVERIFY(QFile::link(outside.filePath("secret.jpg"), dir.filePath("s/link.jpg")));
        QVERIFY(QFile::link(outside.path(), dir.filePath("linked-style")));
        QVERIFY(WallpaperCatalog(dir.path()).entries().isEmpty());
    }

    void absurdDimensionsAreRejected()
    {
        QTemporaryDir const dir;
        // A tiny file that claims a huge picture (decompression bomb): a PNG
        // 9000 x 9000 of one colour compresses to a few kilobytes.
        QVERIFY(writeImage(dir.filePath("s/huge.png"), WallpaperCatalog::MaxDimension + 1, 16, "PNG"));
        QVERIFY(writeImage(dir.filePath("s/ok.png"), WallpaperCatalog::MaxDimension, 16, "PNG"));
        const WallpaperCatalog catalog(dir.path());
        QCOMPARE(catalog.entries().size(), 1);
        QCOMPARE(catalog.entries()[0].name, QStringLiteral("s/ok.png"));
    }

    void theProjectWallpapersAreAccepted()
    {
        // The real folder of the repository, when the tests run from the tree.
        const QString root = QStringLiteral(GALLEON_SOURCE_WALLPAPERS);
        if (!QDir(root).exists())
            QSKIP("public/wallpaper not found");
        const WallpaperCatalog catalog(root);
        QVERIFY(catalog.entries().size() >= 8);
        for (const auto &entry : catalog.entries())
            QVERIFY2(WallpaperCatalog::isAcceptable(entry.path), qPrintable(entry.name));
    }
};

QTEST_GUILESS_MAIN(TestWallpaperCatalog)
#include "test_wallpapercatalog.moc"
