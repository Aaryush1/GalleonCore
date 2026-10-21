// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Required.h"
#include <InitialPages.h>
#include <PagesConfig.h>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>
#include <QSet>

using namespace galleond;
namespace {
QString applicationPath(int index)
{
#ifdef Q_OS_WIN
    return QStringLiteral("C:/apps/player-%1.exe").arg(index);
#else
    return QStringLiteral("/apps/player-%1.desktop").arg(index);
#endif
}
}

class TestInitialPages : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void fiveThemesAndTwelveNumberKeys()
    {
        const QJsonObject root = QJsonDocument::fromJson(windowsInitialPagesJson(QStringLiteral("/absent"), QStringLiteral("/videos"))).object();
        QCOMPARE(root.value("themes").toArray().size(), 5);
        QCOMPARE(root.value("themes").toArray().at(0).toObject().value("name").toString().at(5), QChar(0x00f4));
        const QJsonObject pages = root.value("pages").toObject();
        QCOMPARE(pages.size(), 5);
        QCOMPARE(pages.value("main").toObject().value("keys").toArray().size(), 12);
        QCOMPARE(pages.value("numpad").toObject().value("keys").toArray().size(), 12);
        QCOMPARE(pages.value("multimedia").toObject().value("menu").toObject().value("category").toString(), QStringLiteral("AudioVideo"));
        QCOMPARE(pages.value("jeux").toObject().value("steam").toObject().value("max").toInt(), 12);
        const auto parsed = parsePagesConfig(QJsonDocument(root).toJson());
        QVERIFY2(parsed.config.has_value(), qPrintable(parsed.errors.join('\n')));
        QCOMPARE(required(parsed.config).screensaver.videos.size(), 2);
        QVERIFY(required(parsed.config).screensaver.hasGeometry);
    }
    void automaticLaunchersStayOnOneTwelveKeyPage_data()
    {
        QTest::addColumn<int>("count");
        for (const int count : {0, 1, 12, 13, 22, 23, 80})
            QTest::newRow(qPrintable(QString::number(count))) << count;
    }
    void automaticLaunchersStayOnOneTwelveKeyPage()
    {
        QFETCH(const int, count);
        QList<DesktopApp> apps;
        for (int index = 0; index < count; ++index) {
            DesktopApp app;
            app.file = applicationPath(index);
            app.name = QStringLiteral("Player %1").arg(index);
            app.categories = {QStringLiteral("AudioVideo")};
            apps.append(app);
        }
        const auto original = QJsonDocument::fromJson(windowsInitialPagesJson(QStringLiteral("/absent"), QStringLiteral("/videos"))).object();
        const auto expanded = seedWindowsLaunchers(original, apps, {});
        const auto parsed = parsePagesConfig(QJsonDocument(expanded).toJson());
        QVERIFY2(parsed.config.has_value(), qPrintable(parsed.errors.join('\n')));
        const auto &pages = required(parsed.config).pages;
        QVERIFY(!pages.contains(QStringLiteral("multimedia-plus")));
        QSet<QString> references;
        for (const QString &name : {QStringLiteral("multimedia"), QStringLiteral("multimedia-plus")}) {
            if (!pages.contains(name))
                continue;
            const Page page = pages.value(name);
            for (const auto &key : page.keys) {
                if (key && key->action.type == ActionType::App) {
                    QVERIFY(!references.contains(key->action.desktop));
                    references.insert(key->action.desktop);
                }
            }
        }
        QCOMPARE(references.size(), qMin(count, 12));
        QCOMPARE(pages.size(), 5);
        QCOMPARE(fixedWindowsPages(expanded), expanded);
    }

    void existingNavigationAndCustomLabelsArePreserved()
    {
        auto root = QJsonDocument::fromJson(windowsInitialPagesJson(QStringLiteral("/absent"), QStringLiteral("/videos"))).object();
        QJsonObject pages = root.value("pages").toObject();
        QJsonObject media = pages.value("multimedia").toObject();
        const QJsonObject key{{"key", 11}, {"label", "My control"}, {"action", QJsonObject{{"type", "back"}}}};
        media.insert("keys", QJsonArray{key});
        pages.insert("multimedia", media);
        root.insert("pages", pages);
        const auto frozen = fixedWindowsPages(root).value("pages").toObject().value("multimedia").toObject();
        QCOMPARE(frozen.value("keys"), media.value("keys"));
        QVERIFY(!frozen.contains("menu"));
        QVERIFY(!frozen.contains("sort"));
    }
    void gamesFromSteamAndMenuShareTheTwelveLauncherLimit()
    {
        const auto root = QJsonDocument::fromJson(windowsInitialPagesJson(QStringLiteral("/absent"), QStringLiteral("/videos"))).object();
        QList<SteamGame> games;
        for (int id = 1; id <= 15; ++id)
            games.append(SteamGame{.appid = id, .name = QStringLiteral("Game %1").arg(id)});
        QList<DesktopApp> apps;
        for (int id = 1; id <= 30; ++id) {
            DesktopApp app;
            app.name = QStringLiteral("Game shortcut %1").arg(id);
            app.file = applicationPath(id);
            app.categories = {QStringLiteral("Game")};
            app.steamAppId = id;
            apps.append(app);
        }
        const auto parsed = parsePagesConfig(QJsonDocument(seedWindowsLaunchers(root, apps, games)).toJson());
        QVERIFY(parsed.config.has_value());
        int count = 0;
        for (const QString &name : {QStringLiteral("jeux")}) {
            const Page page = required(parsed.config).pages.value(name);
            for (const auto &key : page.keys) {
                if (key && (key->action.type == ActionType::App || key->action.type == ActionType::Steam))
                    ++count;
            }
        }
        QCOMPARE(count, 12);
    }
    void reloadingNeverRefillsAnEmptySlotOrMovesACustomKey()
    {
        auto root = QJsonDocument::fromJson(windowsInitialPagesJson(QStringLiteral("/absent"), QStringLiteral("/videos"))).object();
        auto pages = root.value("pages").toObject();
        auto page = pages.value("multimedia").toObject();
        const QJsonObject key{{"key", 7}, {"label", "My launcher"},
            {"action", QJsonObject{{"type", "app"}, {"desktop", applicationPath(7)}}}};
        page.insert("keys", QJsonArray{key});
        pages.insert("multimedia", page);
        root.insert("pages", pages);
        const auto frozen = fixedWindowsPages(root);
        const auto parsed = parsePagesConfig(QJsonDocument(frozen).toJson());
        QVERIFY(parsed.config.has_value());
        const Page result = required(parsed.config).pages.value("multimedia");
        QVERIFY(!result.keys[0]);
        QCOMPARE(required(result.keys[7]).label, QStringLiteral("My launcher"));
        QCOMPARE(result.menuMax, 0);
        QCOMPARE(result.steamMax, 0);
        QCOMPARE(result.sort, PageSort::None);
        QCOMPARE(fixedWindowsPages(frozen), frozen);
    }
    void theGeneratedPlusLinkIsRemovedWithoutDeletingItsStoredLaunchers()
    {
        auto root = QJsonDocument::fromJson(windowsInitialPagesJson(QStringLiteral("/absent"), QStringLiteral("/videos"))).object();
        auto pages = root.value("pages").toObject();
        auto media = pages.value("multimedia").toObject();
        media.insert("keys", QJsonArray{QJsonObject{{"key", 11}, {"label", "Multimédia-Plus"},
            {"action", QJsonObject{{"type", "page"}, {"page", "multimedia-plus"}}}}});
        const QJsonObject extra{{"key", 0}, {"label", "Preserved"},
            {"action", QJsonObject{{"type", "app"}, {"desktop", applicationPath(0)}}}};
        pages.insert("multimedia", media);
        pages.insert("multimedia-plus", QJsonObject{{"keys", QJsonArray{extra}}});
        root.insert("pages", pages);
        const auto frozen = fixedWindowsPages(root);
        const auto parsed = parsePagesConfig(QJsonDocument(frozen).toJson());
        QVERIFY(parsed.config.has_value());
        const Page parent = required(parsed.config).pages.value("multimedia");
        QCOMPARE(required(parent.keys[11]).action.type, ActionType::App);
        QCOMPARE(required(parent.keys[11]).label, QStringLiteral("Preserved"));
        QCOMPARE(frozen.value("pages").toObject().value("multimedia-plus").toObject().value("keys").toArray(), QJsonArray{extra});
    }
    void mediaAndGamesHaveUsefulCategories()
    {
        for (const QString &name : {QStringLiteral("Mozilla Firefox"), QStringLiteral("Microsoft Edge"), QStringLiteral("VLC media player"), QStringLiteral("Spotify")})
            QVERIFY(windowsAppCategories(name, QStringLiteral("C:/Programs")).contains(QStringLiteral("AudioVideo")));
        QVERIFY(windowsAppCategories(QStringLiteral("My Game"), QStringLiteral("C:\\Programs\\Games")).contains(QStringLiteral("Game")));
        QVERIFY(windowsAppCategories(QStringLiteral("Game"), QStringLiteral("C:/Programs/Jeux")).contains(QStringLiteral("Game")));
        QCOMPARE(windowsAppCategories(QStringLiteral("Notepad"), QStringLiteral("C:/Programs")), QStringList{QStringLiteral("Utility")});
    }
    void missingWindowsUtilitiesAreNotOffered()
    {
        const QJsonObject root = QJsonDocument::fromJson(windowsInitialPagesJson(QStringLiteral("/absent"), QStringLiteral("/videos"))).object();
        QVERIFY(root.value("pages").toObject().value("systeme").toObject().value("keys").toArray().isEmpty());
    }
#ifdef Q_OS_WIN
    void nativeInitialFileIsValidAndContainsSystemUtilities()
    {
        const auto parsed = parsePagesConfig(initialPagesJson());
        QVERIFY2(parsed.config.has_value(), qPrintable(parsed.errors.join('\n')));
        QCOMPARE(required(parsed.config).themes.size(), size_t(5));
        QVERIFY(required(parsed.config).pages.value("systeme").keys[0].has_value());
    }
#else
    void linuxDefaultsStayUnchanged() { QCOMPARE(initialPagesJson(), defaultPagesJson()); }
#endif
};
QTEST_GUILESS_MAIN(TestInitialPages)
#include "test_initialpages.moc"
