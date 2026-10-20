// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include <InitialPages.h>
#include <PagesConfig.h>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTest>

using namespace galleond;
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
        QCOMPARE(parsed.config->screensaver.videos.size(), 2);
        QVERIFY(parsed.config->screensaver.hasGeometry);
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
        QCOMPARE(parsed.config->themes.size(), size_t(5));
        QVERIFY(parsed.config->pages.value("systeme").keys[0].has_value());
    }
#else
    void linuxDefaultsStayUnchanged() { QCOMPARE(initialPagesJson(), defaultPagesJson()); }
#endif
};
QTEST_GUILESS_MAIN(TestInitialPages)
#include "test_initialpages.moc"
