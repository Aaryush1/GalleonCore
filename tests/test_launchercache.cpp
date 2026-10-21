// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "LauncherCache.h"
#include <QTest>
#include <QSignalSpy>
#include <QTimer>
#include <memory>
using galleond::LauncherCache;
class TestLauncherCache : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void slowShellWorkDoesNotBlockTheEventLoopAndHasADeadline()
    {
        LauncherCache cache(QStringLiteral(GALLEON_TEST_LAUNCHER), 1000);
        QTimer timer;
        int ticks = 0;
        connect(&timer, &QTimer::timeout, this, [&] { ++ticks; });
        timer.start(10);
        QVERIFY(cache.lookup(QStringLiteral("slow")).isEmpty());
        QVERIFY(cache.lookup(QStringLiteral("ready")).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(cache.contains(QStringLiteral("ready")), 5000);
        QVERIFY(ticks >= 2);
        QVERIFY(cache.contains(QStringLiteral("slow")));
        QVERIFY(cache.lookup(QStringLiteral("slow")).isEmpty());
        QCOMPARE(cache.lookup(QStringLiteral("ready")).value("name").toString(), QStringLiteral("ready"));
    }
    void obsoletePageRequestsAreCancelled()
    {
        LauncherCache cache(QStringLiteral(GALLEON_TEST_LAUNCHER), 1000);
        QVERIFY(cache.lookup(QStringLiteral("slow")).isEmpty());
        cache.cancelPending();
        QVERIFY(cache.lookup(QStringLiteral("new page")).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(cache.contains(QStringLiteral("new page")), 5000);
        QVERIFY(!cache.contains(QStringLiteral("slow")));
        QCOMPARE(cache.lookup(QStringLiteral("new page")).value("name").toString(), QStringLiteral("new page"));
    }
    void excessiveOutputIsRejected_data()
    {
        QTest::addColumn<QString>("key");
        QTest::newRow("stdout") << QStringLiteral("flood");
        QTest::newRow("stderr") << QStringLiteral("error-flood");
    }
    void excessiveOutputIsRejected()
    {
        QFETCH(const QString, key);
        LauncherCache cache(QStringLiteral(GALLEON_TEST_LAUNCHER), 1000);
        QVERIFY(cache.lookup(key).isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(cache.contains(key), 5000);
        QVERIFY(cache.lookup(key).isEmpty());
        const auto ready = std::make_unique<QSignalSpy>(&cache, &LauncherCache::ready);
        QTest::qWait(50);
        QCOMPARE(ready->size(), 0); // A rejected result is not retried on every repaint.
    }
};
QTEST_GUILESS_MAIN(TestLauncherCache)
#include "test_launchercache.moc"
