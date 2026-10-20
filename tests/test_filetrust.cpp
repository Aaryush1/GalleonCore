// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "FileTrust.h"
#include <QtTest>
#include <QTemporaryFile>
using namespace galleond;
class TestFileTrust : public QObject
{
    Q_OBJECT
private slots:
    void privateFileCanLaunchApps()
    {
        QTemporaryFile file;
        QVERIFY(file.open());
        QVERIFY(isTrustedPagesFile(file));
    }
#ifndef Q_OS_WIN
    void writableByOthersCannotLaunchApps()
    {
        QTemporaryFile file;
        QVERIFY(file.open());
        QVERIFY(file.setPermissions(file.permissions() | QFileDevice::WriteOther));
        QVERIFY(!isTrustedPagesFile(file));
    }
#endif
};
QTEST_GUILESS_MAIN(TestFileTrust)
#include "test_filetrust.moc"
