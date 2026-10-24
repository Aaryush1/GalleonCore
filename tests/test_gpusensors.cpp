// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include <GpuSensors.h>
#include <QTest>
#include <QTemporaryDir>
using namespace galleond;
class TestGpuSensors : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void cleanup() { qunsetenv("GALLEON_GPU_TEST_MODE"); }
    void unavailableLibrariesAreOptional() {
        const QTemporaryDir dir;
        QVERIFY(readNvmlSensors({dir.filePath("absent-library")}).isEmpty());
        QVERIFY(readAmdSmiSensors({dir.filePath("absent-library")}).isEmpty());
    }
    void nvmlSelectsGpuLoadAndAcceptsRealZero() {
        const QStringList libraries{QStringLiteral(GALLEON_TEST_GPU_LIBRARY)};
        auto data = readNvmlSensors(libraries);
        QCOMPARE(data.value("provider").toString(), QStringLiteral("NVML"));
        QCOMPARE(data.value("gpu").toDouble(), 77.0);
        QCOMPARE(data.value("gpuLoad").toDouble(), 41.0);
        qputenv("GALLEON_GPU_TEST_MODE", "zero");
        data = readNvmlSensors(libraries);
        QVERIFY(data.contains("gpuLoad")); QCOMPARE(data.value("gpuLoad").toDouble(), 0.0);
        qputenv("GALLEON_GPU_TEST_MODE", "partial");
        data = readNvmlSensors(libraries);
        QVERIFY(!data.contains("gpu")); QCOMPARE(data.value("gpuLoad").toDouble(), 41.0);
        qputenv("GALLEON_GPU_TEST_MODE", "invalid");
        QVERIFY(readNvmlSensors(libraries).isEmpty());
        qputenv("GALLEON_GPU_TEST_MODE", "unsupported");
        QVERIFY(readNvmlSensors(libraries).isEmpty());
    }
    void amdSmiUsesGfxActivityAndRejectsUnsupportedSentinels() {
        const QStringList libraries{QStringLiteral(GALLEON_TEST_GPU_LIBRARY)};
        auto data = readAmdSmiSensors(libraries);
        QCOMPARE(data.value("provider").toString(), QStringLiteral("AMD-SMI"));
        QCOMPARE(data.value("gpu").toDouble(), 55.0);
        QCOMPARE(data.value("gpuLoad").toDouble(), 73.0);
        qputenv("GALLEON_GPU_TEST_MODE", "zero");
        data = readAmdSmiSensors(libraries);
        QVERIFY(data.contains("gpuLoad")); QCOMPARE(data.value("gpuLoad").toDouble(), 0.0);
        qputenv("GALLEON_GPU_TEST_MODE", "partial");
        data = readAmdSmiSensors(libraries);
        QVERIFY(!data.contains("gpu")); QCOMPARE(data.value("gpuLoad").toDouble(), 73.0);
        qputenv("GALLEON_GPU_TEST_MODE", "invalid");
        QVERIFY(readAmdSmiSensors(libraries).isEmpty());
        qputenv("GALLEON_GPU_TEST_MODE", "overcount");
        QVERIFY(readAmdSmiSensors(libraries).isEmpty());
    }
};
QTEST_GUILESS_MAIN(TestGpuSensors)
#include "test_gpusensors.moc"
