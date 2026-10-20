// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PageName.h>

#include <QRegularExpression>
#include <QTest>

using bosun::pageNameFor;

class TestPageName : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void aNameBecomesALowerCaseSlug_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<QString>("page");
        QTest::newRow("plain") << "Music" << "music";
        QTest::newRow("accents and space") << "Création Musicale" << "creation-musicale";
        QTest::newRow("symbols") << "Jeux & vidéo!" << "jeux-video";
        QTest::newRow("digits kept") << "Studio 54" << "studio-54";
        QTest::newRow("trailing junk") << "Work --- " << "work";
        QTest::newRow("slashes cannot get in") << "../etc/passwd" << "etc-passwd";
        QTest::newRow("nothing usable") << "!!!" << "page";
        QTest::newRow("empty") << "" << "page";
        QTest::newRow("not latin") << "日本語" << "page";
    }
    void aNameBecomesALowerCaseSlug()
    {
        QFETCH(const QString, name);
        QFETCH(const QString, page);
        QCOMPARE(pageNameFor(name, {}), page);
    }

    void aNameThatIsTakenGetsANumber()
    {
        QCOMPARE(pageNameFor("Music", {"music"}), QStringLiteral("music-2"));
        QCOMPARE(pageNameFor("Music", {"music", "music-2"}), QStringLiteral("music-3"));
        QCOMPARE(pageNameFor("Music", {"music-2"}), QStringLiteral("music"));
    }

    void theNameIsNeverLongerThanAPageName()
    {
        const QString longName(100, 'x');
        QCOMPARE(pageNameFor(longName, {}).size(), 32);
        const QString taken = pageNameFor(longName, {});
        const QString second = pageNameFor(longName, {taken});
        QVERIFY(second.size() <= 32);
        QVERIFY(second != taken);
        QVERIFY(second.endsWith("-2"));
    }

    void whateverIsTypedTheResultIsAValidPageName()
    {
        const QRegularExpression valid("^[a-z0-9_-]{1,32}$");
        for (const QString &name : {QString("A"), QString("-"), QString("é"), QString("a b c"), QString(80, 'z'), QString("\n\t"), QString("Ünï-côdé")})
            QVERIFY2(valid.match(pageNameFor(name, {})).hasMatch(), qPrintable(name));
    }
};

QTEST_GUILESS_MAIN(TestPageName)
#include "test_pagename.moc"
