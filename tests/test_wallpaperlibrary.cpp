// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <WallpaperLibrary.h>

#include <QDir>
#include <QFile>
#include <QImage>
#include <QPainter>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using bosun::WallpaperLibrary;

namespace {

bool writeImage(const QString &path, int width, int height, const QColor &colour, const char *format = "PNG")
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(colour);
    return image.save(path, format);
}

// A picture whose left half is red and right half blue: to see which part was cut.
bool writeTwoColours(const QString &path, int width, int height)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::blue);
    QPainter painter(&image);
    painter.fillRect(0, 0, width / 2, height, Qt::red);
    painter.end();
    return image.save(path, "PNG");
}

struct Setup
{
    QTemporaryDir dir;
    WallpaperLibrary::Places places;
    Setup()
    {
        places.roots = {dir.filePath("project")};
        places.userRoot = dir.filePath("user/wallpaper");
        places.settingsFile = dir.filePath("galleond.conf");
        if (!writeImage(dir.filePath("project/style/a-01.png"), 64, 36, Qt::darkGreen)
            || !writeImage(dir.filePath("project/style/a-02.jpg"), 64, 36, Qt::darkCyan, "JPEG"))
            qFatal("cannot write the test pictures");
    }
};

QStringList namesOf(const QVariantList &list)
{
    QStringList names;
    for (const QVariant &entry : list)
        names << entry.toMap().value("name").toString();
    return names;
}

} // namespace

class TestWallpaperLibrary : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void theWallpapersAreListedWithTheirThumbnailsAndWhoseTheyAre()
    {
        Setup s;
        QVERIFY(writeImage(s.dir.filePath("user/wallpaper/mine/own.png"), 64, 36, Qt::red));
        const WallpaperLibrary library(s.places);
        QCOMPARE(namesOf(library.wallpapers()), (QStringList{"mine/own.png", "style/a-01.png", "style/a-02.jpg"}));
        const QVariantMap own = library.wallpapers().at(0).toMap();
        QVERIFY(own.value("mine").toBool());
        QVERIFY(!library.wallpapers().at(1).toMap().value("mine").toBool());
        QCOMPARE(own.value("thumbnail").toString(), QStringLiteral("image://wallpapers/mine/own.png"));
        const QImage thumb = library.thumbnail("style/a-01.png");
        QVERIFY(!thumb.isNull());
        QVERIFY(library.thumbnail("style/none.png").isNull());
        QVERIFY(library.thumbnail("../../etc/passwd").isNull()); // only a listed name becomes a path
    }

    void aThumbnailIsSmall()
    {
        Setup s;
        QVERIFY(writeImage(s.dir.filePath("project/big/b-01.png"), 3000, 1700, Qt::darkMagenta));
        const WallpaperLibrary library(s.places);
        const QImage thumb = library.thumbnail("big/b-01.png");
        QVERIFY(thumb.width() <= WallpaperLibrary::ThumbnailWidth && thumb.height() <= WallpaperLibrary::ThumbnailWidth);
        QVERIFY(thumb.width() > 100);
    }

    void usingAWallpaperKeepsItForTheNextStart()
    {
        Setup s;
        WallpaperLibrary library(s.places);
        QVERIFY(library.current().isEmpty());
        QSignalSpy changed(&library, &WallpaperLibrary::changed);
        QVERIFY(library.apply("style/a-02.jpg").isEmpty());
        QCOMPARE(library.current(), QStringLiteral("style/a-02.jpg"));
        QVERIFY(changed.size() >= 1);
        // Where galleond reads it.
        QSettings galleond(s.places.settingsFile, QSettings::IniFormat);
        QCOMPARE(galleond.value("wallpaper").toString(), QStringLiteral("style/a-02.jpg"));
        QVERIFY(!library.apply("style/none.png").isEmpty());
        QVERIFY(!library.apply("../../etc/passwd").isEmpty());
        QCOMPARE(library.current(), QStringLiteral("style/a-02.jpg")); // a refusal changes nothing
    }

    void aPictureThatCannotBeUsedIsSaidAndNotAdded()
    {
        Setup s;
        QVERIFY(QFile(s.dir.filePath("notes.txt")).open(QIODevice::WriteOnly));
        WallpaperLibrary library(s.places);
        for (const QString &chosen : {s.dir.filePath("notes.txt"), s.dir.filePath("missing.png"), s.dir.path(), QStringLiteral("file://") + s.dir.filePath("missing.png")}) {
            library.inspect(chosen);
            QCOMPARE(library.candidate().value("state").toString(), QStringLiteral("unusable"));
            QVERIFY(!library.candidate().value("why").toString().isEmpty());
            QVERIFY(!library.addCandidate(0, 0, 10, 10, "x").isEmpty());
        }
        QVERIFY(!QFileInfo::exists(s.places.userRoot)); // nothing was made
    }

    void aPictureThatIsTooLargeInPixelsIsRefused()
    {
        Setup s;
        // A valid PNG header can claim any size: 9000 x 9000 is over the limit.
        QVERIFY(writeImage(s.dir.filePath("wide.png"), 9000, 4, Qt::white)); // small in bytes, wide in pixels
        WallpaperLibrary library(s.places);
        library.inspect(s.dir.filePath("wide.png"));
        QCOMPARE(library.candidate().value("state").toString(), QStringLiteral("unusable"));
    }

    void aPictureIsLookedAtThenCutAndSavedAmongTheUsersOwn()
    {
        Setup s;
        QVERIFY(writeTwoColours(s.dir.filePath("Holiday photo.png"), 2000, 1000)); // red left, blue right
        WallpaperLibrary library(s.places);
        library.inspect(QUrl::fromLocalFile(s.dir.filePath("Holiday photo.png")).toString()); // as a file dialog gives it
        const QVariantMap candidate = library.candidate();
        QCOMPARE(candidate.value("state").toString(), QStringLiteral("ready"));
        QCOMPARE(candidate.value("width").toInt(), 2000);
        QCOMPARE(candidate.value("height").toInt(), 1000);
        QCOMPARE(candidate.value("canvasWidth").toInt(), 720);
        QCOMPARE(candidate.value("canvasHeight").toInt(), 384);
        QVERIFY(candidate.value("preview").toString().startsWith("image://wallpaperpreview/"));
        QVERIFY(!library.candidatePreview().isNull());
        QVERIFY(library.candidatePreview().width() <= WallpaperLibrary::PreviewSide);

        // The left part only: 720:384 proportions, so 750 x 400 at the left edge is all red.
        QVERIFY(library.addCandidate(0, 0, 750, 400, "").isEmpty());
        QVERIFY(library.candidate().isEmpty());
        const QString saved = s.dir.filePath("user/wallpaper/mine/Holiday photo.jpg");
        QVERIFY(QFileInfo::exists(saved));
        const QImage result(saved);
        QCOMPARE(result.size(), QSize(1440, 768));
        QVERIFY(qRed(result.pixel(700, 380)) > 200 && qBlue(result.pixel(700, 380)) < 60);
        QVERIFY(namesOf(library.wallpapers()).contains("mine/Holiday photo.jpg"));
        QVERIFY(galleond::WallpaperCatalog::isAcceptable(saved));
        QVERIFY(library.wallpapers().at(0).toMap().value("mine").toBool()); // listed first: "mine" sorts before "style"

        // The right part under the same name: a new file, the first one stays.
        library.inspect(s.dir.filePath("Holiday photo.png"));
        QVERIFY(library.addCandidate(1250, 600, 750, 400, "Holiday photo").isEmpty());
        QVERIFY(QFileInfo::exists(s.dir.filePath("user/wallpaper/mine/Holiday photo-2.jpg")));
        QVERIFY(qBlue(QImage(s.dir.filePath("user/wallpaper/mine/Holiday photo-2.jpg")).pixel(700, 380)) > 200);
    }

    void aCutThatIsAskedForBadlyIsMadeValid()
    {
        Setup s;
        QVERIFY(writeTwoColours(s.dir.filePath("p.png"), 800, 800));
        WallpaperLibrary library(s.places);
        library.inspect(s.dir.filePath("p.png"));
        // Way out of the picture and of the wrong proportions: still a valid 720:384 part of it.
        QVERIFY(library.addCandidate(-500, 5000, 99999, 3, "../../escape").isEmpty());
        QVERIFY(!QFileInfo::exists(s.dir.filePath("escape")));
        const QStringList saved = QDir(s.dir.filePath("user/wallpaper/mine")).entryList(QDir::Files);
        QCOMPARE(saved.size(), 1);
        QVERIFY(!saved.at(0).contains('/'));
        QCOMPARE(QImage(s.dir.filePath("user/wallpaper/mine/" + saved.at(0))).size(), QSize(1440, 768));
    }

    void onlyTheUsersOwnPicturesCanBeDeletedAndNotTheOneInUse()
    {
        Setup s;
        QVERIFY(writeImage(s.dir.filePath("user/wallpaper/mine/own.png"), 64, 36, Qt::red));
        QVERIFY(writeImage(s.dir.filePath("user/wallpaper/mine/other.png"), 64, 36, Qt::blue));
        WallpaperLibrary library(s.places);
        QVERIFY(!library.removeMine("style/a-01.png").isEmpty()); // the project's
        QVERIFY(QFileInfo::exists(s.dir.filePath("project/style/a-01.png")));
        QVERIFY(!library.removeMine("../../etc/passwd").isEmpty());
        QVERIFY(!library.removeMine("mine/none.png").isEmpty());
        QVERIFY(library.apply("mine/own.png").isEmpty());
        QVERIFY(!library.removeMine("mine/own.png").isEmpty()); // in use
        QVERIFY(QFileInfo::exists(s.dir.filePath("user/wallpaper/mine/own.png")));
        QVERIFY(library.removeMine("mine/other.png").isEmpty());
        QVERIFY(!QFileInfo::exists(s.dir.filePath("user/wallpaper/mine/other.png")));
        QVERIFY(!namesOf(library.wallpapers()).contains("mine/other.png"));
    }

    void aLinkedFileIsNeverTakenForTheUsersOwn()
    {
        Setup s;
        QDir().mkpath(s.dir.filePath("user/wallpaper/mine"));
        QVERIFY(QFile::link(s.dir.filePath("project/style/a-01.png"), s.dir.filePath("user/wallpaper/mine/link.png")));
        const WallpaperLibrary library(s.places);
        QVERIFY(!namesOf(library.wallpapers()).contains("mine/link.png")); // the catalog skips links
        QVERIFY(QFileInfo::exists(s.dir.filePath("project/style/a-01.png")));
    }

    void thePicturesAddedByAnotherProgramAreSeenAfterAReload()
    {
        Setup s;
        WallpaperLibrary library(s.places);
        QCOMPARE(library.wallpapers().size(), 2);
        QVERIFY(writeImage(s.dir.filePath("user/wallpaper/mine/new.png"), 64, 36, Qt::red));
        QCOMPARE(library.wallpapers().size(), 2);
        library.reload();
        QCOMPARE(library.wallpapers().size(), 3);
    }
};

QTEST_GUILESS_MAIN(TestWallpaperLibrary)
#include "test_wallpaperlibrary.moc"
