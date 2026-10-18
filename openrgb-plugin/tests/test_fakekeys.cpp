// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "FakeKeys.h"

#include <QTest>

using namespace galleon::openrgb;

class TestFakeKeys : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void indexBoundaries()
    {
        QVERIFY(isValidLedIndex(0));
        QVERIFY(isValidLedIndex(FakeLedCount - 1));
        QVERIFY(!isValidLedIndex(FakeLedCount));
    }

    void namesAreOneBased()
    {
        QCOMPARE(QString::fromStdString(fakeLedName(0)), QStringLiteral("Key 1"));
        QCOMPARE(QString::fromStdString(fakeLedName(11)), QStringLiteral("Key 12"));
    }

    void outOfRangeGivesEmptyName()
    {
        QVERIFY(fakeLedName(FakeLedCount).empty());
        QVERIFY(fakeLedName(0xFFFFFFFFu).empty());
    }
};

QTEST_GUILESS_MAIN(TestFakeKeys)
#include "test_fakekeys.moc"
