// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <SteamLibrary.h>
#include <UsageStats.h>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QTimeZone>

using namespace galleond;

namespace {

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    file.write(bytes);
    return true;
}

const QDateTime T0 = QDateTime::fromSecsSinceEpoch(1'700'000'000, QTimeZone::utc());

const QByteArray LocalConfig = R"("UserLocalConfigStore"
{
	"Software"
	{
		"Valve"
		{
			"Steam"
			{
				"apps"
				{
					"730"
					{
						"LastPlayed"		"1700000000"
						"Playtime"		"600"
					}
					"220"
					{
						"Playtime"		"90"
						"LastPlayed"		"1799000000"
					}
					"notanumber"
					{
						"Playtime"		"5"
					}
				}
			}
		}
	}
}
)";

QByteArray manifest(int appid, const QString &name)
{
    return QStringLiteral("\"AppState\"\n{\n\t\"appid\"\t\t\"%1\"\n\t\"name\"\t\t\"%2\"\n}\n").arg(appid).arg(name).toUtf8();
}

} // namespace

class TestUsage : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void aLaunchCountsOneAndFadesByHalfEveryThirtyDays()
    {
        UsageStats stats;
        QCOMPARE(stats.score("a.desktop", T0), 0.0);
        stats.record("a.desktop", T0);
        QCOMPARE(stats.score("a.desktop", T0), 1.0);
        QVERIFY(qAbs(stats.score("a.desktop", T0.addDays(30)) - 0.5) < 1e-9);
        QVERIFY(qAbs(stats.score("a.desktop", T0.addDays(60)) - 0.25) < 1e-9);
        stats.record("a.desktop", T0.addDays(30)); // 0.5 + 1
        QVERIFY(qAbs(stats.score("a.desktop", T0.addDays(30)) - 1.5) < 1e-9);
    }

    void aRecentLaunchBeatsAnOldHabit()
    {
        UsageStats stats;
        for (int i = 0; i < 5; ++i)
            stats.record("old.desktop", T0.addDays(i - 400)); // five launches over a year ago
        stats.record("new.desktop", T0.addDays(-1));
        QVERIFY(stats.score("new.desktop", T0) > stats.score("old.desktop", T0));
    }

    void aDateInTheFutureCountsAsNow()
    {
        UsageStats stats;
        stats.record("a.desktop", T0.addDays(10));
        QVERIFY(qAbs(stats.score("a.desktop", T0) - 1.0) < 1e-9);
    }

    void theScoresAreKeptInTheFile()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("share/usage.json");
        {
            UsageStats stats(file);
            stats.record("a.desktop", T0);
            stats.record("a.desktop", T0);
            stats.record("steam:730", T0);
        }
        QVERIFY(QFile::exists(file));
        const UsageStats again(file);
        QCOMPARE(again.size(), qsizetype(2));
        QVERIFY(qAbs(again.score("a.desktop", T0) - 2.0) < 1e-9);
        QVERIFY(qAbs(again.score("steam:730", T0) - 1.0) < 1e-9);
    }

    void theNumberOfEntriesIsBoundedAndTheLeastUsedGoFirst()
    {
        UsageStats stats;
        stats.record("keep.desktop", T0);
        stats.record("keep.desktop", T0);
        for (int i = 0; i < UsageStats::MaxEntries + 20; ++i)
            stats.record(QStringLiteral("app%1.desktop").arg(i), T0);
        QVERIFY(stats.size() <= UsageStats::MaxEntries);
        QVERIFY(stats.score("keep.desktop", T0) > 1.5); // the most used one stayed
    }

    void aBadFileIsIgnoredEntryByEntry()
    {
        QTemporaryDir dir;
        const QString file = dir.filePath("usage.json");
        QVERIFY(writeFile(file, "{\"version\":1,\"entries\":{"
                                "\"good.desktop\":{\"score\":2,\"at\":1700000000},"
                                "\"negative.desktop\":{\"score\":-5,\"at\":1700000000},"
                                "\"huge.desktop\":{\"score\":1e12,\"at\":1700000000},"
                                "\"future.desktop\":{\"score\":1,\"at\":99999999999},"
                                "\"text.desktop\":{\"score\":\"many\",\"at\":1},"
                                "\"\":{\"score\":1,\"at\":1}}}"));
        const UsageStats stats(file);
        QCOMPARE(stats.size(), qsizetype(1));
        QVERIFY(stats.score("good.desktop", QDateTime::fromSecsSinceEpoch(1700000000)) > 1.9);

        QVERIFY(writeFile(file, "not json"));
        QCOMPARE(UsageStats(file).size(), qsizetype(0));
        QVERIFY(writeFile(file, "{\"version\":2,\"entries\":{\"a\":{\"score\":1,\"at\":1}}}"));
        QCOMPARE(UsageStats(file).size(), qsizetype(0));
        QVERIFY(writeFile(file, QByteArray(UsageStats::MaxFileBytes + 10, 'x')));
        QCOMPARE(UsageStats(file).size(), qsizetype(0));
    }

    void aTooLongIdIsRefused()
    {
        UsageStats stats;
        stats.record(QString(UsageStats::MaxIdLength + 1, 'x'), T0);
        stats.record({}, T0);
        QCOMPARE(stats.size(), qsizetype(0));
    }

    // ---- Steam

    void theVdfFormatIsReadWithAnyCaseOfKeys()
    {
        const auto root = SteamLibrary::parseVdf(LocalConfig);
        QVERIFY(root.has_value());
        const VdfNode *apps = root->child("userlocalconfigstore")->child("SOFTWARE")->child("valve")->child("steam")->child("apps");
        QVERIFY(apps);
        QVERIFY(apps->child("730"));
        QCOMPARE(apps->child("730")->value("playtime"), QStringLiteral("600"));
    }

    void malformedVdfIsRefused()
    {
        QVERIFY(!SteamLibrary::parseVdf("\"a\" {").has_value());                 // not closed
        QVERIFY(!SteamLibrary::parseVdf("\"a\" \"b").has_value());               // unterminated string
        QVERIFY(!SteamLibrary::parseVdf("} \"a\" \"b\"").has_value());           // closes nothing
        QVERIFY(!SteamLibrary::parseVdf("garbage").has_value());
        QVERIFY(!SteamLibrary::parseVdf("\"a\"").has_value());                   // a key with nothing
        QByteArray deep;
        for (int i = 0; i < 40; ++i)
            deep += "\"k\" {";
        for (int i = 0; i < 40; ++i)
            deep += "}";
        QVERIFY(!SteamLibrary::parseVdf(deep).has_value());                      // too deep
        QVERIFY(!SteamLibrary::parseVdf(QByteArray(SteamLibrary::MaxVdfBytes + 1, ' ')).has_value());
        QVERIFY(!SteamLibrary::parseVdf("\"" + QByteArray(5000, 'k') + "\" \"v\"").has_value()); // a key that long is not real
        QVERIFY(SteamLibrary::parseVdf("").has_value());                         // an empty file is an empty tree
    }

    void playTimeAndLastLaunchComeFromTheLocalConfig()
    {
        const auto games = SteamLibrary::parseLocalConfig(LocalConfig);
        QCOMPARE(games.size(), 2); // "notanumber" is left out
        QCOMPARE(games.value(730).playtimeMinutes, qint64(600));
        QCOMPARE(games.value(730).lastPlayed, qint64(1700000000));
        QCOMPARE(games.value(220).playtimeMinutes, qint64(90));
        QVERIFY(SteamLibrary::parseLocalConfig("garbage").isEmpty());
    }

    void aManifestGivesTheNameAndIdAndNothingElseIsTrusted()
    {
        const auto game = SteamLibrary::parseManifest(manifest(730, "Counter-Strike 2"));
        QVERIFY(game.has_value());
        QCOMPARE(game->appid, 730);
        QCOMPARE(game->name, QStringLiteral("Counter-Strike 2"));
        QVERIFY(!SteamLibrary::parseManifest(manifest(0, "Zero")).has_value());
        QVERIFY(!SteamLibrary::parseManifest(manifest(-5, "Negative")).has_value());
        QVERIFY(!SteamLibrary::parseManifest(manifest(5, "")).has_value());
        QVERIFY(!SteamLibrary::parseManifest("\"AppState\" { \"appid\" \"x\" \"name\" \"y\" }").has_value());
        QVERIFY(!SteamLibrary::parseManifest("\"Other\" { }").has_value());
        QCOMPARE(SteamLibrary::parseManifest(manifest(7, QString(300, 'n')))->name.size(), 80);
    }

    void toolsAreNotGames()
    {
        for (const char *name : {"Proton 9.0", "Proton Experimental", "Steam Linux Runtime 3.0 (sniper)",
                                 "Steamworks Common Redistributables", "Some Redistributable"}) {
            SteamGame tool;
            tool.appid = 5;
            tool.name = name;
            QVERIFY2(SteamLibrary::isTool(tool), name);
        }
        SteamGame game;
        game.appid = 730;
        game.name = "Counter-Strike 2";
        QVERIFY(!SteamLibrary::isTool(game));
    }

    void libraryFoldersMustBeAbsolutePaths()
    {
        const auto folders = SteamLibrary::parseLibraryFolders(
            "\"libraryfolders\" { \"0\" { \"path\" \"/home/u/.steam\" } \"1\" { \"path\" \"/mnt/games/Steam\" } "
            "\"2\" { \"path\" \"relative/path\" } \"3\" { \"label\" \"no path\" } }");
        QCOMPARE(folders, (QStringList{"/home/u/.steam", "/mnt/games/Steam"}));
    }

    void installedGamesJoinManifestsAndPlayData()
    {
        QTemporaryDir dir;
        const QString root = dir.filePath("Steam");
        const QString second = dir.filePath("Library2");
        QVERIFY(writeFile(root + "/userdata/12345/config/localconfig.vdf", LocalConfig));
        QVERIFY(writeFile(root + "/steamapps/libraryfolders.vdf",
                          QStringLiteral("\"libraryfolders\" { \"0\" { \"path\" \"%1\" } \"1\" { \"path\" \"%2\" } }").arg(root, second).toUtf8()));
        QVERIFY(writeFile(root + "/steamapps/appmanifest_730.acf", manifest(730, "Counter-Strike 2")));
        QVERIFY(writeFile(root + "/steamapps/appmanifest_2805730.acf", manifest(2805730, "Proton 9.0")));
        QVERIFY(writeFile(second + "/steamapps/appmanifest_220.acf", manifest(220, "Half-Life 2")));
        QVERIFY(writeFile(second + "/steamapps/appmanifest_9.acf", "garbage"));

        const QList<SteamGame> games = SteamLibrary::installedGames(root);
        QCOMPARE(games.size(), 2); // the tool and the broken manifest are left out
        QHash<int, SteamGame> byId;
        for (const SteamGame &g : games)
            byId.insert(g.appid, g);
        QVERIFY(byId.contains(730) && byId.contains(220));
        QCOMPARE(byId.value(730).playtimeMinutes, qint64(600));
        QCOMPARE(byId.value(220).lastPlayed, qint64(1799000000));

        QVERIFY(SteamLibrary::installedGames("/nonexistent").isEmpty());
        QVERIFY(SteamLibrary::installedGames({}).isEmpty());
    }

    void theSteamScoreIsHoursFadedBySinceTheLastLaunch()
    {
        SteamGame game;
        game.playtimeMinutes = 600; // 10 hours
        game.lastPlayed = T0.toSecsSinceEpoch();
        QVERIFY(qAbs(SteamLibrary::score(game, T0) - 10.0) < 1e-9);
        QVERIFY(qAbs(SteamLibrary::score(game, T0.addDays(30)) - 5.0) < 1e-9);
        game.lastPlayed = 0; // never launched: faded to nothing
        QVERIFY(SteamLibrary::score(game, T0) < 1e-6);
    }
};

QTEST_GUILESS_MAIN(TestUsage)
#include "test_usage.moc"
