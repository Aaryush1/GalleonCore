// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <CoverLoader.h>

#include <QBuffer>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using galleond::CoverLoader;

namespace {

QByteArray imageBytes(int width, int height, const char *format, QColor colour = Qt::red)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(colour);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, format))
        qFatal("cannot encode the test image");
    return bytes;
}

QString writeFile(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        qFatal("cannot write the test file");
    file.write(bytes);
    return path;
}

// A fetcher that records the requests and answers when the test says so.
struct FakeNetwork
{
    QList<QUrl> requests;
    QList<std::function<void(const QByteArray &)>> pending;

    CoverLoader::Fetcher fetcher()
    {
        return [this](const QUrl &url, const std::function<void(const QByteArray &)> &done) {
            requests.append(url);
            pending.append(done);
        };
    }
    void answerAll(const QByteArray &bytes)
    {
        const auto callbacks = pending;
        pending.clear();
        for (const auto &done : callbacks)
            done(bytes);
    }
};

} // namespace

class TestCoverLoader : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void theAddressPolicy_data()
    {
        QTest::addColumn<QString>("url");
        QTest::addColumn<bool>("allowed");
        QTest::newRow("cdn https") << "https://i.scdn.co/image/ab67616d0000b273" << true;
        QTest::newRow("https with port") << "https://covers.example.org:8443/a.jpg" << true;
        QTest::newRow("local file") << "file:///tmp/cover.jpg" << true;
        QTest::newRow("plain http") << "http://i.scdn.co/image/x" << false;
        QTest::newRow("ipv4") << "https://127.0.0.1/a.jpg" << false;
        QTest::newRow("private ipv4") << "https://192.168.1.5/a.jpg" << false;
        QTest::newRow("link local") << "https://169.254.169.254/latest/meta-data" << false;
        QTest::newRow("ipv6") << "https://[::1]/a.jpg" << false;
        QTest::newRow("decimal ip") << "https://2130706433/a.jpg" << false;
        QTest::newRow("hex ip") << "https://0x7f000001/a.jpg" << false;
        QTest::newRow("localhost") << "https://localhost/a.jpg" << false;
        QTest::newRow("sub localhost") << "https://x.localhost/a.jpg" << false;
        QTest::newRow("mdns") << "https://printer.local/a.jpg" << false;
        QTest::newRow("internal") << "https://api.internal/a.jpg" << false;
        QTest::newRow("lan") << "https://nas.lan/a.jpg" << false;
        QTest::newRow("single label") << "https://intranet/a.jpg" << false;
        QTest::newRow("user info") << "https://user:pw@example.com/a.jpg" << false;
        QTest::newRow("remote file host") << "file://otherhost/etc/passwd" << false;
        QTest::newRow("ftp") << "ftp://example.com/a.jpg" << false;
        QTest::newRow("data") << "data:image/png;base64,AAAA" << false;
        QTest::newRow("javascript") << "javascript:alert(1)" << false;
        QTest::newRow("empty") << "" << false;
        QTest::newRow("not a url") << "no scheme at all" << false;
    }
    void theAddressPolicy()
    {
        QFETCH(QString, url);
        QFETCH(bool, allowed);
        QCOMPARE(CoverLoader::isAllowedUrl(QUrl(url)), allowed);
    }

    void remoteCanBeSwitchedOff()
    {
        QVERIFY(!CoverLoader::isAllowedUrl(QUrl("https://i.scdn.co/a.jpg"), false));
        QVERIFY(CoverLoader::isAllowedUrl(QUrl("file:///tmp/a.jpg"), false));
    }

    void onlyRealSaneImagesAreDecoded()
    {
        QVERIFY(!CoverLoader::decode(imageBytes(64, 64, "PNG")).isNull());
        QVERIFY(!CoverLoader::decode(imageBytes(64, 64, "JPEG")).isNull());
        QVERIFY(CoverLoader::decode({}).isNull());
        QVERIFY(CoverLoader::decode("not an image").isNull());
        QVERIFY(CoverLoader::decode(imageBytes(64, 64, "BMP")).isNull());   // format not accepted
        QVERIFY(CoverLoader::decode(imageBytes(CoverLoader::MaxDimension + 1, 16, "PNG")).isNull());
        QVERIFY(!CoverLoader::decode(imageBytes(CoverLoader::MaxDimension, 16, "PNG")).isNull());
        QVERIFY(CoverLoader::decode(QByteArray(CoverLoader::MaxBytes + 1, 'x')).isNull());
        QVERIFY(CoverLoader::decode(imageBytes(64, 64, "PNG").left(30)).isNull()); // truncated
    }

    void bigCoversAreScaledDown()
    {
        const QImage cover = CoverLoader::decode(imageBytes(2000, 1000, "PNG"));
        QVERIFY(!cover.isNull());
        QVERIFY(cover.width() <= CoverLoader::MaxSide && cover.height() <= CoverLoader::MaxSide);
        QCOMPARE(cover.width(), CoverLoader::MaxSide);
        QCOMPARE(cover.height(), CoverLoader::MaxSide / 2); // the ratio is kept
    }

    void aLocalFileIsLoaded()
    {
        QTemporaryDir dir;
        const QString file = writeFile(dir.filePath("cover.png"), imageBytes(100, 100, "PNG", Qt::green));
        CoverLoader loader;
        QSignalSpy changed(&loader, &CoverLoader::coverChanged);
        loader.request(QUrl::fromLocalFile(file).toString());
        QCOMPARE(changed.count(), 1);
        QVERIFY(!loader.cover().isNull());
        QCOMPARE(loader.cover().pixelColor(50, 50), QColor(Qt::green));
    }

    void badLocalFilesGiveNoCover()
    {
        QTemporaryDir dir;
        CoverLoader loader;
        const auto none = [&](const QString &path) {
            loader.request(QUrl::fromLocalFile(path).toString());
            return loader.cover().isNull();
        };
        QVERIFY(none(writeFile(dir.filePath("text.jpg"), "this is a text file")));
        QVERIFY(none(writeFile(dir.filePath("empty.png"), "")));
        QVERIFY(none(dir.filePath("missing.png")));
        QVERIFY(none(dir.path())); // a folder
        QVERIFY(none(writeFile(dir.filePath("big.png"), QByteArray(CoverLoader::MaxBytes + 1, 'x'))));
        QVERIFY(none(QStringLiteral("/etc/passwd")));
    }

    void aRemoteCoverIsFetchedOnceAndKept()
    {
        FakeNetwork network;
        CoverLoader loader(nullptr, network.fetcher());
        QSignalSpy changed(&loader, &CoverLoader::coverChanged);

        loader.request("https://covers.example.org/a.png");
        QCOMPARE(network.requests.size(), 1);
        QVERIFY(loader.cover().isNull()); // not there yet
        network.answerAll(imageBytes(80, 80, "PNG", Qt::blue));
        QVERIFY(!loader.cover().isNull());
        QCOMPARE(changed.count(), 1);

        loader.request("https://covers.example.org/a.png"); // same address: nothing to do
        QCOMPARE(network.requests.size(), 1);
        QCOMPARE(changed.count(), 1);
    }

    void aFailedDownloadGivesNoCover()
    {
        FakeNetwork network;
        CoverLoader loader(nullptr, network.fetcher());
        loader.request("https://covers.example.org/a.png");
        network.answerAll({});
        QVERIFY(loader.cover().isNull());
        loader.request("https://covers.example.org/b.png");
        network.answerAll("garbage that is not an image");
        QVERIFY(loader.cover().isNull());
    }

    void refusedAddressesNeverReachTheNetwork()
    {
        FakeNetwork network;
        CoverLoader loader(nullptr, network.fetcher());
        for (const QString &url : {QStringLiteral("http://covers.example.org/a.png"), QStringLiteral("https://127.0.0.1/a.png"),
                                   QStringLiteral("https://localhost/a.png"), QStringLiteral("ftp://x.example.org/a.png")})
            loader.request(url);
        QCOMPARE(network.requests.size(), 0);
    }

    void remoteCoversCanBeSwitchedOff()
    {
        QTemporaryDir dir;
        FakeNetwork network;
        CoverLoader loader(nullptr, network.fetcher());
        loader.setRemoteAllowed(false);
        loader.request("https://covers.example.org/a.png");
        QCOMPARE(network.requests.size(), 0);
        loader.request(QUrl::fromLocalFile(writeFile(dir.filePath("c.png"), imageBytes(40, 40, "PNG"))).toString());
        QVERIFY(!loader.cover().isNull());
    }

    void aLateAnswerForAnOldTrackIsIgnored()
    {
        QTemporaryDir dir;
        FakeNetwork network;
        CoverLoader loader(nullptr, network.fetcher());
        loader.request("https://covers.example.org/slow.png");
        // The track changes before the download ends.
        loader.request(QUrl::fromLocalFile(writeFile(dir.filePath("new.png"), imageBytes(40, 40, "PNG", Qt::yellow))).toString());
        QCOMPARE(loader.cover().pixelColor(5, 5), QColor(Qt::yellow));
        network.answerAll(imageBytes(40, 40, "PNG", Qt::red)); // the old one arrives
        QCOMPARE(loader.cover().pixelColor(5, 5), QColor(Qt::yellow));
    }

    void clearingRemovesTheCover()
    {
        QTemporaryDir dir;
        CoverLoader loader;
        loader.request(QUrl::fromLocalFile(writeFile(dir.filePath("c.png"), imageBytes(40, 40, "PNG"))).toString());
        QVERIFY(!loader.cover().isNull());
        QSignalSpy changed(&loader, &CoverLoader::coverChanged);
        loader.request({});
        QVERIFY(loader.cover().isNull());
        QCOMPARE(changed.count(), 1);
        loader.request({}); // nothing more to clear
        QCOMPARE(changed.count(), 1);
    }

    void theCacheIsBounded()
    {
        FakeNetwork network;
        CoverLoader loader(nullptr, network.fetcher());
        const int total = CoverLoader::CacheEntries + 2;
        for (int i = 0; i < total; ++i) {
            loader.request(QStringLiteral("https://covers.example.org/%1.png").arg(i));
            network.answerAll(imageBytes(30, 30, "PNG", QColor::fromHsv(i * 20, 255, 255)));
        }
        QCOMPARE(network.requests.size(), total);

        loader.request(QStringLiteral("https://covers.example.org/%1.png").arg(total - 2)); // recent: cached
        QCOMPARE(network.requests.size(), total);
        loader.request(QStringLiteral("https://covers.example.org/0.png")); // oldest: forgotten
        QCOMPARE(network.requests.size(), total + 1);
    }
};

QTEST_MAIN(TestCoverLoader)
#include "test_coverloader.moc"
