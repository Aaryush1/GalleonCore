// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

// The whole chain as galleond wires it: a player on D-Bus (MPRIS), the MPRIS
// client, the screen controller, and a deck that records what it is sent.

#include "FakeTransport.h"

#include <MprisController.h>
#include <ScreenController.h>
#include <ScreenRenderer.h>
#include <galleon/StreamDeck.h>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace galleond;

namespace {

const QString WidgetsDir = QStringLiteral(GALLEON_SOURCE_WIDGETS);

class FakePlayer : public QObject
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(qlonglong Position READ position)

public:
    explicit FakePlayer(QString name)
        : m_name(std::move(name))
    {
        auto bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_name);
        m_ok = bus.registerService(m_name)
               && bus.registerObject(QStringLiteral("/org/mpris/MediaPlayer2"), this,
                                     QDBusConnection::ExportAllSlots | QDBusConnection::ExportAllProperties);
    }
    ~FakePlayer() override
    {
        {
            auto bus = QDBusConnection::connectToBus(QDBusConnection::SessionBus, m_name);
            bus.unregisterObject(QStringLiteral("/org/mpris/MediaPlayer2"));
            bus.unregisterService(m_name);
        }
        QDBusConnection::disconnectFromBus(m_name);
    }
    [[nodiscard]] bool ok() const { return m_ok; }
    [[nodiscard]] QString playbackStatus() const { return m_status; }
    [[nodiscard]] QVariantMap metadata() const { return m_metadata; }
    [[nodiscard]] qlonglong position() const { return m_position; }
    void set(const QString &status, const QVariantMap &metadata, qlonglong position)
    {
        m_status = status;
        m_metadata = metadata;
        m_position = position;
    }

private:
    QString m_name;
    QString m_status = QStringLiteral("Stopped");
    QVariantMap m_metadata;
    qlonglong m_position = 0;
    bool m_ok = false;
};

bool near(QRgb a, QRgb b, int tolerance = 28)
{
    return qAbs(qRed(a) - qRed(b)) <= tolerance && qAbs(qGreen(a) - qGreen(b)) <= tolerance
           && qAbs(qBlue(a) - qBlue(b)) <= tolerance;
}

QImage lastScreen(const FakeTransport &transport)
{
    const auto &writes = transport.writes;
    qsizetype last = -1;
    for (qsizetype i = static_cast<qsizetype>(writes.size()) - 1; i >= 0; --i) {
        const auto &r = writes[static_cast<std::size_t>(i)];
        if (r.size() > 10 && r[0] == 0x02 && r[1] == 0x0c && r[10] == 1) {
            last = i;
            break;
        }
    }
    if (last < 0)
        return {};
    QByteArray jpeg;
    for (qsizetype i = last; i >= 0; --i) {
        const auto &r = writes[static_cast<std::size_t>(i)];
        if (r.size() < 16 || r[1] != 0x0c)
            break;
        jpeg.prepend(reinterpret_cast<const char *>(r.data()) + 16, r[13] | (r[14] << 8));
        if ((r[11] | (r[12] << 8)) == 0)
            break;
    }
    QImage image;
    image.loadFromData(jpeg, "JPEG");
    return image;
}

} // namespace

class TestNowPlayingFlow : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        if (!QDBusConnection::sessionBus().isConnected())
            QSKIP("no D-Bus session bus");
    }

    void aPlayerOnDBusMakesTheScreenShowTheTrack()
    {
        const QString name = QStringLiteral("org.mpris.MediaPlayer2.galleonflow%1").arg(QCoreApplication::applicationPid());
        FakePlayer player(name);
        QVERIFY(player.ok());

        QTemporaryDir dir;
        QImage green(1600, 900, QImage::Format_RGB32);
        green.fill(QColor(30, 160, 60));
        QDir().mkpath(dir.filePath("wall/style"));
        QVERIFY(green.save(dir.filePath("wall/style/green.png")));

        auto fake = std::make_unique<FakeTransport>();
        FakeTransport *transport = fake.get();
        galleon::StreamDeck deck(std::move(fake));
        QVERIFY(deck.open());
        ScreenRenderer wallpaperRenderer(WidgetsDir + "/WallpaperClock.qml");
        ScreenRenderer nowPlayingRenderer(WidgetsDir + "/NowPlaying.qml");
        QVERIFY(wallpaperRenderer.isValid() && nowPlayingRenderer.isValid());

        MprisController media(QDBusConnection::sessionBus(), name);
        ScreenController controller(&deck, &wallpaperRenderer, WallpaperCatalog(dir.filePath("wall")),
                                    WidgetsDir + "/WallpaperClock.qml", &media, &nowPlayingRenderer);
        ScreenConfig config;
        config.nowPlaying = NowPlayingMode::PlayingOrPaused; // what the user's file asks for
        controller.setScreenConfig(config);
        QVERIFY(controller.SetWallpaper(QStringLiteral("style/green.png")));
        QVERIFY(near(lastScreen(*transport).pixel(705, 200), qRgb(30, 160, 60))); // the wallpaper first

        // The player starts playing a track.
        player.set(QStringLiteral("Playing"),
                   {{"xesam:title", "Flow Song"}, {"xesam:artist", QStringList{"Flow Artist"}}, {"mpris:length", qlonglong(120'000'000)}},
                   30'000'000);
        QTRY_VERIFY_WITH_TIMEOUT(near(lastScreen(*transport).pixel(705, 200), qRgb(0x14, 0x17, 0x1c)), 6000);

        // Paused: the user's mode keeps the track, with a grey bar.
        player.set(QStringLiteral("Paused"),
                   {{"xesam:title", "Flow Song"}, {"xesam:artist", QStringList{"Flow Artist"}}, {"mpris:length", qlonglong(120'000'000)}},
                   31'000'000);
        QTRY_VERIFY_WITH_TIMEOUT(near(lastScreen(*transport).pixel(388 + 15, 317), qRgb(0x8a, 0x92, 0x9d)), 6000);

        // Stopped: back to the wallpaper.
        player.set(QStringLiteral("Stopped"), {}, 0);
        QTRY_VERIFY_WITH_TIMEOUT(near(lastScreen(*transport).pixel(705, 200), qRgb(30, 160, 60)), 6000);
    }
};

QTEST_MAIN(TestNowPlayingFlow)
#include "test_nowplaying_flow.moc"
