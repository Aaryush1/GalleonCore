// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <FileNames.h>

#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace bosun::filenames;

class TestFileNames : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void aNameIsMadeSafeAndTheFallbackIsWhatTheCallerSays()
    {
        QCOMPARE(safeName("Night Rain"), QStringLiteral("Night Rain"));
        QCOMPARE(safeName("a/b\\c:d"), QStringLiteral("a-b-c-d"));
        QCOMPARE(safeName(" ..x"), QStringLiteral("x"));
        QCOMPARE(safeName(""), QStringLiteral("file"));
        QCOMPARE(safeName("***", "picture"), QStringLiteral("---")); // symbols are kept as dashes: still a name
        QCOMPARE(safeName("...", "picture"), QStringLiteral("picture"));
        QCOMPARE(safeName(QString(300, 'z')).size(), MaxNameLength);
    }

    void aFreePathKeepsTheSuffixAndNeverTakesAFileThatIsThere()
    {
        const QTemporaryDir dir;
        QCOMPARE(freePath(dir.path(), "a", ".jpg"), dir.filePath("a.jpg"));
        QVERIFY(QFile(dir.filePath("a.jpg")).open(QIODevice::WriteOnly));
        QCOMPARE(freePath(dir.path(), "a", ".jpg"), dir.filePath("a-2.jpg"));
        QCOMPARE(freePath(dir.path(), "a", ".mp4"), dir.filePath("a.mp4")); // another suffix: another name
    }
};

QTEST_GUILESS_MAIN(TestFileNames)
#include "test_filenames.moc"
