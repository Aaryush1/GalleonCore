// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include <GameMode.h>
#include <HomeWidgets.h>
#include <ScreenRenderer.h>
#include <QFile>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>
using namespace galleond;
class TestGameMode : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void captureControlsModeAndMissingSensorsAreExplicit() {
        GameMode mode;
        FpsSample sample{144.0, {}}; sample.process = "galleon-fixture.exe"; sample.token = "frame1";
        const auto active = mode.update(sample, {});
        QVERIFY(active.value("active").toBool());
        QCOMPARE(active.value("gpu").toString(), QString(QChar(0x2014)));
        QVERIFY(!mode.update(sample, {{"automatic", false}}).value("active").toBool());
        sample.cpuTemperature = 54; sample.gpuTemperature = 62;
        sample.cpuLoad = 37; sample.gpuLoad = 91;
        const auto temperatures = mode.update(sample, {});
        QVERIFY(temperatures.value("cpu").toString().contains("54"));
        QVERIFY(temperatures.value("gpu").toString().contains("62"));
        QVERIFY(!mode.update({}, {}).value("active").toBool());
    }
    void localArtworkAndSettingsAreAppliedAtomically() {
        const QTemporaryDir dir;
        const auto image = dir.filePath("art.png");
        QImage art(720, 384, QImage::Format_RGB32); art.fill(QColor("#204080")); QVERIFY(art.save(image));
        HomeWidgets editor(nullptr, dir.filePath("widgets.json"), true);
        QVERIFY(editor.setGameOptions(true, true));
        QVERIFY(editor.setGameProfile("Galleon-Fixture.exe", "Test Game", image));
        QVERIFY(!QFile::exists(dir.filePath("widgets.json")));
        QVERIFY(editor.apply());
        const HomeWidgets stored(nullptr, dir.filePath("widgets.json"));
        QVERIFY(stored.gameConfig().value("localSensors").toBool());
        GameMode mode;
        FpsSample sample{144.0, {}}; sample.process = "galleon-fixture.exe"; sample.token = "1";
        sample.cpuTemperature = 54; sample.gpuTemperature = 62;
        sample.cpuLoad = 37; sample.gpuLoad = 91;
        const auto state = mode.update(sample, stored.gameConfig());
        QCOMPARE(state.value("title").toString(), QStringLiteral("Test Game"));
        QVERIFY(!state.value("image").toString().isEmpty());
        ScreenRenderer renderer(QStringLiteral(GALLEON_SOURCE_WIDGETS "/WallpaperClock.qml"));
        const auto frame = renderer.render({{"gameState", state}, {"live", false}}, QSize(720, 384));
        QVERIFY(!frame.isNull());
        int cpuArc = 0;
        int gpuArc = 0;
        for (int y = 134; y < 361; ++y) {
            for (int x = 252; x < 461; ++x) {
                const auto color = frame.pixelColor(x, y);
                if (color.red() < 160 && color.green() > 160 && color.blue() > 130) ++cpuArc;
            }
            for (int x = 482; x < 691; ++x) {
                const auto color = frame.pixelColor(x, y);
                if (color.red() > 200 && color.green() < 160 && color.blue() < 160) ++gpuArc;
            }
        }
        QVERIFY(cpuArc > 30); // dial arcs must be present in offscreen software rendering
        QVERIFY(gpuArc > 30);
        const auto output = qEnvironmentVariable("GALLEON_GAME_SCREENSHOT");
        if (!output.isEmpty()) QVERIFY(frame.save(output));
        const auto home = renderer.render({{"gameState", QVariantMap()}}, QSize(720, 384));
        QVERIFY(home != frame);
    }
    void loadPercentagesValidateAndFallBackIndependently() {
        GameMode mode;
        FpsSample sample{144.0, {}}; sample.process = "galleon-fixture.exe"; sample.token = "load1";
        auto state = mode.update(sample, {});
        QVERIFY(!state.contains("cpuLoad"));
        QVERIFY(!state.contains("gpuLoad"));
        sample.cpuLoad = 0; sample.gpuLoad = 100;
        state = mode.update(sample, {});
        QCOMPARE(state.value("cpuLoad").toDouble(), 0.0);
        QCOMPARE(state.value("gpuLoad").toDouble(), 100.0);
        sample.cpuLoad = -1; sample.gpuLoad = 101;
        state = mode.update(sample, {}, {{"cpuLoad", 48.0}, {"gpuLoad", 72.0}});
        QCOMPARE(state.value("cpuLoad").toDouble(), 48.0);
        QCOMPARE(state.value("gpuLoad").toDouble(), 72.0);
        state = mode.update(sample, {}, {{"cpuLoad", "bad"}, {"gpuLoad", 200.0}});
        QVERIFY(!state.contains("cpuLoad"));
        QVERIFY(!state.contains("gpuLoad"));
    }
    void localMonitorSelectsTotalLoadNotMemoryOrCores() {
        const auto values = GameMode::parseSensors(R"({"Children":[
          {"SensorId":"/intelcpu/0/load/0","Value":"43.5 %"},
          {"SensorId":"/intelcpu/0/load/1","Value":"99.0 %"},
          {"SensorId":"/gpu-nvidia/0/load/0","Value":"0.0 %"},
          {"SensorId":"/gpu-nvidia/0/load/1","Value":"88.0 %"}]})");
        QCOMPARE(values.value("cpuLoad").toDouble(), 43.5);
        QVERIFY(values.contains("gpuLoad"));
        QCOMPARE(values.value("gpuLoad").toDouble(), 0.0);
    }
    void sensorJsonIsBoundedAndValidated() {
        const auto data = QByteArray(R"({"Children":[{"SensorId":"/intelcpu/0/temperature/0","Value":"55.0 °C"},{"SensorId":"/gpu-nvidia/0/temperature/0","Value":"140.0 °F"},{"SensorId":"/nvme/0/temperature/0","Value":"90 °C"}]})");
        const auto values = GameMode::parseSensors(data);
        QCOMPARE(values.value("cpu").toDouble(), 55.0);
        QCOMPARE(values.value("gpu").toDouble(), 60.0);
        QVERIFY(GameMode::parseSensors("{}").isEmpty());
        QVERIFY(GameMode::parseSensors(QByteArray(1024 * 1024 + 1, ' ')).isEmpty());
    }
    void missingMangoHudGpuTelemetryUsesNativeLoad() {
        const QTemporaryDir dir;
        const auto path = dir.filePath("cs2.csv");
        const auto write = [&](const QByteArray &row) {
            QFile file(path);
            return file.open(QIODevice::WriteOnly) && file.write("fps,cpu_load,gpu_load,gpu_temp,gpu_core_clock,gpu_vram_used,elapsed\n" + row) > 0;
        };
        QVERIFY(write("127,22,0,0,0,0,1000\n"));
        auto sample = readFpsLog(path);
        QVERIFY(sample.fps);
        QVERIFY(!sample.gpuLoad); // CS2 capture with GPU name but no actual GPU telemetry
        GameMode mode;
        auto state = mode.update(sample, {}, {{"gpuLoad", 66.0}, {"gpu", 55.0}});
        QCOMPARE(state.value("gpuLoad").toDouble(), 66.0);
        QVERIFY(state.value("gpu").toString().contains("55"));
        QVERIFY(write("127,22,0,40,210,1,1500\n"));
        sample = readFpsLog(path);
        QVERIFY(sample.gpuLoad); // a real idle GPU remains a valid 0%, not missing
        QCOMPARE(sample.gpuLoad.value_or(-1.0), 0.0);
        state = mode.update(sample, {}, {{"gpuLoad", 66.0}});
        QCOMPARE(state.value("gpuLoad").toDouble(), 0.0);
    }
    void amdFallbackReadsTheHwmonLayout() {
        QTemporaryDir dir;
        QVERIFY(QDir(dir.path()).mkpath("hwmon3/device"));
        const auto write = [&](const QString &name, const QByteArray &data) {
            QFile file(dir.filePath("hwmon3/" + name));
            return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
        };
        QVERIFY(write("name", "amdgpu\n"));
        QVERIFY(write("temp1_input", "55000\n"));
        QVERIFY(write("device/gpu_busy_percent", "73\n"));
        const auto values = GameMode::readAmdGpuSensors(dir.path());
        QCOMPARE(values.value("gpu").toDouble(), 55.0);
        QCOMPARE(values.value("gpuLoad").toDouble(), 73.0);
        QVERIFY(write("device/gpu_busy_percent", "101\n"));
        QVERIFY(!GameMode::readAmdGpuSensors(dir.path()).contains("gpuLoad"));
    }
    void csvCarriesGameAndTemperatures() {
        const QTemporaryDir dir;
        QFile f(dir.filePath("fixture_2026-10-23_12-00-00.csv"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("fps,cpu_temp,gpu_temp,cpu_load,gpu_load,elapsed\n144,54,62,37,91,1000\n"); f.close();
        const auto sample = readFpsLog(dir.path());
        QCOMPARE(sample.process, QStringLiteral("fixture"));
        QCOMPARE(sample.cpuLoad.value_or(-1), 37.0);
        QCOMPARE(sample.gpuLoad.value_or(-1), 91.0);
        QCOMPARE(sample.cpuTemperature.value_or(-1), 54.0);
        QCOMPARE(sample.gpuTemperature.value_or(-1), 62.0);
        const auto stale = readFpsLog(dir.path(), {}, QDateTime::currentDateTime().addSecs(11));
        GameMode mode;
        QVERIFY(!mode.update(stale, {}).value("active").toBool());
    }
};
QTEST_MAIN(TestGameMode)
#include "test_gamemode.moc"
