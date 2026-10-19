// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PageOrder.h>

#include <QTest>
#include <QTimeZone>

using namespace galleond;

namespace {

const QDateTime Now = QDateTime::fromSecsSinceEpoch(1'700'000'000, QTimeZone::UTC);

KeyConfig appKey(int key, const QString &desktop)
{
    KeyConfig k;
    k.key = key;
    k.action.type = ActionType::App;
    k.action.desktop = desktop;
    return k;
}

DesktopApp menuApp(const QString &id, const QString &name, int steamAppId = 0)
{
    DesktopApp app;
    app.file = "/x/" + id;
    app.id = id;
    app.name = name;
    app.steamAppId = steamAppId;
    return app;
}

SteamGame played(int appid, qint64 minutes, int daysAgo, const QString &name = {})
{
    SteamGame g;
    g.appid = appid;
    g.name = name;
    g.playtimeMinutes = minutes;
    g.lastPlayed = Now.addDays(-daysAgo).toSecsSinceEpoch();
    return g;
}

QString desktopAt(const ArrangedPage &arranged, int key)
{
    const auto &k = arranged.page.keys[static_cast<std::size_t>(key)];
    return k ? k->action.desktop : QString();
}

} // namespace

class TestPageOrder : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void aPageWithoutOptionsIsLeftAsItIs()
    {
        Page page;
        page.keys[0] = appKey(0, "b.desktop");
        page.keys[1] = appKey(1, "a.desktop");
        UsageStats usage;
        usage.record("a.desktop", Now);
        const ArrangedPage arranged = arrangePage(page, usage, {}, Now);
        QCOMPARE(desktopAt(arranged, 0), QStringLiteral("b.desktop"));
        QCOMPARE(arranged.highlightKey, -1);
    }

    void sortingPutsTheMostUsedFirstInTheSamePlaces()
    {
        Page page;
        page.sort = PageSort::Usage;
        page.keys[1] = appKey(1, "a.desktop");
        page.keys[4] = appKey(4, "b.desktop");
        page.keys[7] = appKey(7, "c.desktop");
        KeyConfig media; // not an application: it stays where it is
        media.key = 2;
        page.keys[2] = media;
        UsageStats usage;
        usage.record("c.desktop", Now);
        usage.record("c.desktop", Now);
        usage.record("b.desktop", Now);
        const ArrangedPage arranged = arrangePage(page, usage, {}, Now);
        QCOMPARE(desktopAt(arranged, 1), QStringLiteral("c.desktop"));
        QCOMPARE(desktopAt(arranged, 4), QStringLiteral("b.desktop"));
        QCOMPARE(desktopAt(arranged, 7), QStringLiteral("a.desktop"));
        QVERIFY(arranged.page.keys[2].has_value());
        QCOMPARE(arranged.page.keys[1]->key, 1); // a key knows its place
        QCOMPARE(arranged.highlightKey, 1);
    }

    void withNoUseTheFileOrderStaysAndNothingIsMarked()
    {
        Page page;
        page.sort = PageSort::Usage;
        page.keys[0] = appKey(0, "a.desktop");
        page.keys[1] = appKey(1, "b.desktop");
        const ArrangedPage arranged = arrangePage(page, UsageStats(), {}, Now);
        QCOMPARE(desktopAt(arranged, 0), QStringLiteral("a.desktop"));
        QCOMPARE(arranged.highlightKey, -1);
    }

    void theMenuFillsTheFreeKeysMostPlayedFirstAndSkipsTools()
    {
        Page page;
        page.menuCategory = "Game";
        page.menuMax = 3;
        page.keys[0] = appKey(0, "steam.desktop"); // already there: kept, not repeated
        ArrangeSources sources;
        sources.menu = {menuApp("steam.desktop", "Steam"),
                        menuApp("old.desktop", "Old Favourite", 11),
                        menuApp("recent.desktop", "Recent", 22),
                        menuApp("proton.desktop", "Proton Experimental"),
                        menuApp("snake.desktop", "Snake"),
                        menuApp("zzz.desktop", "Zzz")};
        sources.steamOfApp = {{"old.desktop", 11}, {"recent.desktop", 22}};
        sources.playedSteam = {{11, played(11, 6000, 700)}, {22, played(22, 300, 1)}};
        const ArrangedPage arranged = arrangePage(page, UsageStats(), sources, Now);
        QCOMPARE(desktopAt(arranged, 0), QStringLiteral("steam.desktop"));
        QCOMPARE(desktopAt(arranged, 1), QStringLiteral("recent.desktop")); // 5 h yesterday beats 100 h two years ago
        // The next ones: no play data and no use, so by name (the tool is left out).
        QCOMPARE(desktopAt(arranged, 2), QStringLiteral("old.desktop")); // its 100 h faded to almost nothing, still above 0
        QCOMPARE(desktopAt(arranged, 3), QStringLiteral("snake.desktop"));
        QVERIFY(!arranged.page.keys[4].has_value()); // menuMax 3: three were added
    }

    void aSteamShortcutAndTheSameSteamGameAreShownOnce()
    {
        Page page;
        page.menuCategory = "Game";
        page.menuMax = 2;
        page.steamMax = 3;
        ArrangeSources sources;
        sources.menu = {menuApp("cs2.desktop", "Counter-Strike 2", 730)};
        sources.steamOfApp = {{"cs2.desktop", 730}};
        sources.installedSteam = {played(730, 100, 1, "Counter-Strike 2"), played(10, 50, 1, "Other Game")};
        sources.playedSteam = {{730, sources.installedSteam[0]}, {10, sources.installedSteam[1]}};
        const ArrangedPage arranged = arrangePage(page, UsageStats(), sources, Now);
        int count730 = 0;
        for (const auto &key : arranged.page.keys) {
            if (key && (key->action.appid == 730 || key->action.desktop == "cs2.desktop"))
                ++count730;
        }
        QCOMPARE(count730, 1); // the menu shortcut, and Steam's own entry was skipped
        QCOMPARE(desktopAt(arranged, 0), QStringLiteral("cs2.desktop"));
        QVERIFY(arranged.page.keys[1].has_value());
        QVERIFY(arranged.page.keys[1]->action.type == ActionType::Steam);
        QCOMPARE(arranged.page.keys[1]->action.appid, 10);
    }

    void aShortcutIsScoredByThePlayTimeOfItsSteamGame()
    {
        ArrangeSources sources;
        sources.steamOfApp = {{"cs2.desktop", 730}};
        sources.playedSteam = {{730, played(730, 600, 0)}};
        QVERIFY(qAbs(keyScore(appKey(0, "cs2.desktop"), UsageStats(), sources, Now) - 10.0) < 1e-6);
        QCOMPARE(keyScore(appKey(0, "other.desktop"), UsageStats(), sources, Now), 0.0);
        KeyConfig steam;
        steam.action.type = ActionType::Steam;
        steam.action.appid = 730;
        UsageStats usage;
        usage.record("steam:730", Now);
        QVERIFY(qAbs(keyScore(steam, usage, sources, Now) - 11.0) < 1e-6); // its hours, plus one launch
    }

    void theLimitsAreRespected()
    {
        Page page;
        page.menuCategory = "Game";
        page.menuMax = 12;
        ArrangeSources sources;
        for (int i = 0; i < 40; ++i)
            sources.menu.append(menuApp(QStringLiteral("g%1.desktop").arg(i, 2, 10, QLatin1Char('0')), QStringLiteral("Game %1").arg(i)));
        const ArrangedPage arranged = arrangePage(page, UsageStats(), sources, Now);
        int filled = 0;
        for (const auto &key : arranged.page.keys)
            filled += key ? 1 : 0;
        QCOMPARE(filled, 12);
    }
};

QTEST_GUILESS_MAIN(TestPageOrder)
#include "test_pageorder.moc"
