// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include <HomeWidgets.h>
#include <ScreenRenderer.h>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QGuiApplication>
#include <QTemporaryDir>
#include <QTest>
#include <QDir>
#include <QScopeGuard>
using galleond::HomeWidgets;
class TestHomeWidgets : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void fpsFolderPreparationUsesUserDataAndKeepsDraftUntilApply() {
        const QTemporaryDir dir;
#ifndef Q_OS_WIN
        const auto oldData = qgetenv("XDG_DATA_HOME");
        const auto restore = qScopeGuard([oldData] { if (oldData.isNull()) qunsetenv("XDG_DATA_HOME"); else qputenv("XDG_DATA_HOME",oldData); });
        qputenv("XDG_DATA_HOME",dir.filePath("data").toUtf8());
#endif
        HomeWidgets editor(nullptr,dir.filePath("widgets.json"),true);
        QVERIFY(editor.recommendedFpsFolder().endsWith("/galleoncore/fps"));
        QVERIFY(editor.steamLaunchOptions().contains("mangohud %command%"));
#ifndef Q_OS_WIN
        QCOMPARE(editor.recommendedFpsFolder(),dir.filePath("data/galleoncore/fps"));
        QVERIFY(editor.prepareFpsFolder());
        QVERIFY(QDir(editor.recommendedFpsFolder()).exists());
        QCOMPARE(editor.fpsConfig().value("source").toString(),editor.recommendedFpsFolder());
        QVERIFY(editor.pending());
        QVERIFY(!QFile::exists(dir.filePath("widgets.json")));
        QVERIFY(editor.apply());
#endif
    }
#ifndef Q_OS_WIN
    void mangoHudConfigurationPreservesOptionsAndIsRepeatable() {
        const QTemporaryDir dir;
        const auto config = qgetenv("XDG_CONFIG_HOME");
        const auto data = qgetenv("XDG_DATA_HOME");
        const auto restore = qScopeGuard([config, data] {
            if (config.isNull()) qunsetenv("XDG_CONFIG_HOME"); else qputenv("XDG_CONFIG_HOME", config);
            if (data.isNull()) qunsetenv("XDG_DATA_HOME"); else qputenv("XDG_DATA_HOME", data);
        });
        qputenv("XDG_CONFIG_HOME", dir.filePath("config").toUtf8());
        qputenv("XDG_DATA_HOME", dir.filePath("data").toUtf8());
        HomeWidgets editor(nullptr, dir.filePath("widgets.json"), true);
        QVERIFY(editor.configureMangoHud());
        const auto path = dir.filePath("config/MangoHud/MangoHud.conf");
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        const QByteArray original("# my options\nfps_limit=90\n  log_interval = 1000\noutput_folder=/tmp/old\n");
        file.write(original); file.close();
        QVERIFY(editor.configureMangoHud());
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto updated = file.readAll(); file.close();
        QVERIFY(updated.contains("# my options\nfps_limit=90\n"));
        QVERIFY(!updated.contains("1000"));
        QCOMPARE(updated.count("log_interval="), 1);
        QVERIFY(updated.contains(editor.recommendedFpsFolder().toUtf8()));
        const auto backups = QDir(QFileInfo(path).absolutePath()).entryList({"*.galleon-backup-*"}, QDir::Files);
        QCOMPARE(backups.size(), 1);
        QFile backup(QFileInfo(path).absolutePath() + "/" + backups.first());
        QVERIFY(backup.open(QIODevice::ReadOnly)); QCOMPARE(backup.readAll(), original);
        QVERIFY(editor.configureMangoHud());
        QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), updated);
        QCOMPARE(QDir(QFileInfo(path).absolutePath()).entryList({"*.galleon-backup-*"}, QDir::Files).size(), 1);
        QVERIFY(!editor.pending());
        const HomeWidgets reader(nullptr, dir.filePath("widgets.json"));
        QCOMPARE(reader.fpsConfig().value("source").toString(), editor.recommendedFpsFolder());
    }
#endif
#ifdef Q_OS_LINUX
    void flatpakPermissionsOnlyForInstalledGameLaunchers() {
        const QTemporaryDir dir;
        QVERIFY(QDir().mkpath(dir.filePath("bin")));
        const auto oldPath = qgetenv("PATH");
        const auto oldData = qgetenv("XDG_DATA_HOME");
        const auto oldConfig = qgetenv("XDG_CONFIG_HOME");
        const auto oldLog = qgetenv("FLATPAK_TEST_LOG");
        const auto oldFail = qgetenv("FLATPAK_TEST_FAIL");
        const auto restore = qScopeGuard([oldPath, oldData, oldConfig, oldLog, oldFail] {
            const auto reset = [](const char *key, const QByteArray &value) {
                if (value.isNull()) qunsetenv(key); else qputenv(key, value);
            };
            reset("PATH", oldPath);
            reset("XDG_DATA_HOME", oldData);
            reset("XDG_CONFIG_HOME", oldConfig);
            reset("FLATPAK_TEST_LOG", oldLog);
            reset("FLATPAK_TEST_FAIL", oldFail);
        });
        const QString fake = dir.filePath("bin/flatpak");
        QFile script(fake);
        QVERIFY(script.open(QIODevice::WriteOnly));
        script.write(R"(#!/bin/sh
if [ "$1" = list ]; then
    printf '%s\n' com.heroicgameslauncher.hgl com.usebottles.bottles net.lutris.Lutris com.valvesoftware.Steam
    exit 0
fi
if [ "$1" = override ]; then
    if [ "$FLATPAK_TEST_FAIL" = 1 ]; then echo "permission refused" >&2; exit 1; fi
    printf '%s\n' "$@" > "$FLATPAK_TEST_LOG"
    exit 0
fi
exit 2
)");
        script.close();
        QVERIFY(QFile::setPermissions(fake, QFile::permissions(fake) | QFileDevice::ExeUser));
        const QString log = dir.filePath("flatpak.args");
        qputenv("PATH", (dir.filePath("bin") + QLatin1Char(':') + QString::fromUtf8(oldPath)).toUtf8());
        qputenv("XDG_DATA_HOME", dir.filePath("data").toUtf8());
        qputenv("XDG_CONFIG_HOME", dir.filePath("config").toUtf8());
        qputenv("FLATPAK_TEST_LOG", log.toUtf8());
        qunsetenv("FLATPAK_TEST_FAIL");
        HomeWidgets editor(nullptr, dir.filePath("widgets.json"), true);
        const auto launchers = editor.flatpakGameLaunchers();
        QCOMPARE(launchers.size(), 4);
        QCOMPARE(launchers.first().toMap().value("name").toString(), QStringLiteral("Heroic"));
        QVERIFY(!editor.allowFlatpakFpsAccess(QStringLiteral("org.unknown.App")));
        QVERIFY(!editor.allowFlatpakFpsAccess(QStringLiteral("com.heroicgameslauncher.hgl")));
        QVERIFY(!QFile::exists(log));
        QVERIFY(editor.configureMangoHud());
        for (const auto &launcher : launchers) {
            const auto id = launcher.toMap().value("id").toString();
            QVERIFY2(editor.allowFlatpakFpsAccess(id), qPrintable(editor.error()));
            QFile argsFile(log);
            QVERIFY(argsFile.open(QIODevice::ReadOnly));
            const QByteArray args = argsFile.readAll();
            QVERIFY(args.contains("override\n--user\n"));
            QVERIFY(args.contains(("--filesystem=" + dir.filePath("config/MangoHud") + ":ro").toUtf8()));
            QVERIFY(args.contains(("--filesystem=" + editor.recommendedFpsFolder()).toUtf8()));
            QVERIFY(args.contains(("--env=MANGOHUD_CONFIGFILE=" + dir.filePath("config/MangoHud/MangoHud.conf")).toUtf8()));
            QVERIFY(args.endsWith(id.toUtf8() + "\n"));
            QCOMPARE(args.count("--filesystem="), 2);
            argsFile.close();
        }
        qputenv("FLATPAK_TEST_FAIL", "1");
        QVERIFY(!editor.allowFlatpakFpsAccess(QStringLiteral("com.valvesoftware.Steam")));
        QVERIFY(editor.error().contains(QStringLiteral("permission refused")));
    }
#endif
    void defaultsAndCrossProcessReload() {
        const QTemporaryDir dir;
        HomeWidgets writer(nullptr, dir.filePath("widgets.json"));
        HomeWidgets reader(nullptr, dir.filePath("widgets.json"));
        QCOMPARE(writer.layout().size(), 7);
        QVERIFY(writer.layout()[0].toMap().value("enabled").toBool());
        for (int i = 1; i < writer.layout().size(); ++i) QVERIFY(!writer.layout()[i].toMap().value("enabled").toBool());
        QVERIFY(writer.setWidget("cpu", true, 400, 20, 200, 60));
        QTRY_COMPARE_WITH_TIMEOUT(reader.layout()[2].toMap().value("x").toInt(), 400, 3500);
        QVERIFY(reader.layout()[2].toMap().value("enabled").toBool());
        QVERIFY(reader.reset());
        const HomeWidgets restored(nullptr, dir.filePath("widgets.json"));
        QVERIFY(!restored.layout()[2].toMap().value("enabled").toBool());
    }
    void cityLabelPersistsAndSurvivesCoordinateChanges() {
        const QTemporaryDir dir;
        const auto file = dir.filePath("widgets.json");
        HomeWidgets widgets(nullptr, file);
        QVERIFY(widgets.setCity("  Saint-Étienne  "));
        QCOMPARE(widgets.values().value("weatherCity").toString(), QStringLiteral("Saint-Étienne"));
        QVERIFY(widgets.setLocation("45.44", "4.39"));
        HomeWidgets restored(nullptr, file);
        QCOMPARE(restored.location().value("city").toString(), QStringLiteral("Saint-Étienne"));
        QCOMPARE(restored.values().value("weatherCity").toString(), QStringLiteral("Saint-Étienne"));
        QVERIFY(!restored.setCity(QString(81, QChar('a'))));
        QVERIFY(!restored.setCity("One\nTwo"));
        QVERIFY(restored.setCity(""));
        QVERIFY(!restored.values().contains("weatherCity"));
    }
    void draftChangesReachTheDaemonOnlyAfterApply() {
        const QTemporaryDir dir;
        const auto file = dir.filePath("widgets.json");
        HomeWidgets editor(nullptr, file, true);
        QVERIFY(editor.setWidget("cpu", true, 400, 20, 200, 60));
        QVERIFY(editor.setCity("Lyon"));
        QVERIFY(editor.pending());
        QVERIFY(!QFile::exists(file));
        const HomeWidgets before(nullptr, file);
        QVERIFY(!before.layout()[2].toMap().value("enabled").toBool());
        QVERIFY(editor.apply());
        QVERIFY(!editor.pending());
        const HomeWidgets after(nullptr, file);
        QVERIFY(after.layout()[2].toMap().value("enabled").toBool());
        QCOMPARE(after.values().value("weatherCity").toString(), QStringLiteral("Lyon"));
        QVERIFY(editor.setCity("Paris"));
        const HomeWidgets unchanged(nullptr, file);
        QCOMPARE(unchanged.values().value("weatherCity").toString(), QStringLiteral("Lyon"));
    }
    void fpsAndFontSettingsUseTheApplyBoundary() {
        const QTemporaryDir dir;
        const auto file = dir.filePath("widgets.json");
        HomeWidgets editor(nullptr, file, true);
        QVERIFY(editor.setFpsSource(dir.path(), "Game.exe"));
        QVERIFY(editor.setWidgetTextSize("fps", 40));
        QVERIFY(!editor.setWidgetTextSize("fps", 100));
        QVERIFY(!editor.setFpsSource("relative.csv", ""));
        QVERIFY(!QFile::exists(file));
        QVERIFY(editor.apply());
        HomeWidgets reader(nullptr, file);
        QCOMPARE(reader.fpsConfig().value("source").toString(), dir.path());
        QCOMPARE(reader.fpsConfig().value("process").toString(), QStringLiteral("Game.exe"));
        QCOMPARE(reader.layout().last().toMap().value("textSize").toInt(), 40);
        QVERIFY(reader.setWidget("fps", true, 400, 20, 200, 60));
        QCOMPARE(reader.layout().last().toMap().value("textSize").toInt(), 40);
    }
#ifdef Q_OS_WIN
    void bosunObservesFreshDaemonStateAndRejectsStaleState() {
        const QTemporaryDir dir;
        const auto config = dir.filePath("widgets.json");
        const auto write = [&](qint64 at) {
            QFile file(config + ".runtime.json");
            if (!file.open(QIODevice::WriteOnly)) return false;
            file.write(QJsonDocument(QJsonObject{{"at", at},
                {"capture", QJsonObject{{"state", "capturing"}, {"message", "Fixture active"}}},
                {"game", QJsonObject{{"active", true}, {"title", "Fixture"}}}}).toJson());
            return true;
        };
        QVERIFY(write(QDateTime::currentMSecsSinceEpoch()));
        HomeWidgets editor(nullptr, config, true);
        QVERIFY(editor.gameState().value("active").toBool());
        QCOMPARE(editor.captureStatus().value("state").toString(), QStringLiteral("capturing"));
        QVERIFY(write(QDateTime::currentMSecsSinceEpoch() - 20000));
        QTRY_COMPARE(editor.captureStatus().value("state").toString(), QStringLiteral("offline"));
        QVERIFY(!editor.gameState().value("active").toBool());
    }
#endif
    void managedCaptureSettingsAreStagedAndValidated() {
        const QTemporaryDir dir;
        const auto file = dir.filePath("widgets.json");
        HomeWidgets editor(nullptr, file, true);
        QVERIFY(!editor.setCaptureOptions(true, "relative.exe"));
        QVERIFY(editor.setCaptureOptions(false, dir.filePath("console.exe")));
        QVERIFY(!QFile::exists(file));
        QVERIFY(editor.apply());
        const HomeWidgets reader(nullptr, file);
        QVERIFY(!reader.gameConfig().value("managedCapture").toBool());
        QCOMPARE(reader.gameConfig().value("presentMon").toString(), dir.filePath("console.exe"));
    }
    void rejectsInvalidGeometryAndLocation() {
        const QTemporaryDir dir;
        HomeWidgets widgets(nullptr, dir.filePath("widgets.json"));
        const auto before = widgets.layout();
        QVERIFY(!widgets.setWidget("cpu", true, -1, 0, 200, 60));
        QVERIFY(!widgets.setWidget("cpu", true, 600, 0, 200, 60));
        QVERIFY(!widgets.setWidget("cpu", true, 0, 0, 0, 60));
        QVERIFY(!widgets.setWidget("unknown", true, 0, 0, 200, 60));
        QCOMPARE(widgets.layout(), before);
        QVERIFY(!widgets.setLocation("nan", "0"));
        QVERIFY(!widgets.setLocation("91", "0"));
        QVERIFY(!widgets.setLocation("48,85", "2.35"));
        QVERIFY(!widgets.error().isEmpty());
        QVERIFY(widgets.setLocation("48.85", "2.35")); // weather disabled: no network request
        QVERIFY(widgets.error().isEmpty());
    }
    void clampsHandEditedFiles() {
        const QTemporaryDir dir;
        QFile file(dir.filePath("widgets.json"));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(R"({"layout":[{"id":"cpu","enabled":true,"x":900,"y":-50,"width":9999,"height":2}]})");
        file.close();
        const HomeWidgets widgets(nullptr, file.fileName());
        const auto cpu = widgets.layout()[2].toMap();
        QCOMPARE(cpu.value("width").toInt(), 720);
        QCOMPARE(cpu.value("height").toInt(), 40);
        QCOMPARE(cpu.value("x").toInt(), 0);
        QCOMPARE(cpu.value("y").toInt(), 0);
    }
    void disabledWidgetsDoNotPolluteTheDefaultScreen() {
        const QTemporaryDir dir;
        const HomeWidgets widgets(nullptr, dir.filePath("widgets.json"));
        QVERIFY(widgets.values().isEmpty());
    }
    void rendersReadableSmallCards() {
        galleond::ScreenRenderer renderer(QStringLiteral(GALLEON_SOURCE_WIDGETS "/WallpaperClock.qml"));
        const QStringList ids{"weather", "cpu", "memory", "temperature", "downloads", "fps"};
        QVariantList layout;
        for (int i = 0; i < ids.size(); ++i)
            layout.append(QVariantMap{{"id", ids[i]}, {"enabled", true}, {"x", 20 + (i % 2) * 350}, {"y", 30 + (i / 2) * 110}, {"width", 330}, {"height", 80}, {"textSize", 32}});
        const auto frame = renderer.render({{"widgetLayout", layout}, {"widgetValues", QVariantMap{{"weatherCity", "Tours"}, {"weather", "13,7 °C"}, {"cpu", "3 %"}, {"memory", "29 %"}, {"temperature", "38,0 °C"}, {"downloads", "0 file(s)"}, {"fps", "144 FPS"}}}, {"live", false}}, QSize(720, 384));
        QVERIFY(!frame.isNull());
        const auto output = qEnvironmentVariable("GALLEON_CARDS_SCREENSHOT");
        if (!output.isEmpty()) QVERIFY(frame.save(output));
        layout[0] = QVariantMap{{"id", "weather"}, {"enabled", true}, {"x", 20}, {"y", 30}, {"width", 330}, {"height", 80}, {"textSize", 12}};
        QVERIFY(renderer.render({{"widgetLayout", layout}}, QSize(720, 384)) != frame);
    }
    void rendersCardsAndHonoursVisibility() {
        const QString file = QStringLiteral(GALLEON_SOURCE_WIDGETS "/WallpaperClock.qml");
        galleond::ScreenRenderer renderer(file);
        QVERIFY(renderer.isValid());
        QVariantList layout{QVariantMap{{"id", "cpu"}, {"enabled", true}, {"x", 400}, {"y", 20}, {"width", 220}, {"height", 80}}};
        const auto shown = renderer.render({{"widgetLayout", layout}, {"widgetValues", QVariantMap{{"cpu", "42 %"}}}, {"live", false}}, QSize(720, 384));
        QVERIFY(!shown.isNull());
        layout[0] = QVariantMap{{"id", "cpu"}, {"enabled", false}, {"x", 400}, {"y", 20}, {"width", 220}, {"height", 80}};
        const auto hidden = renderer.render({{"widgetLayout", layout}}, QSize(720, 384));
        QVERIFY(shown != hidden);
        QCOMPARE(hidden.pixelColor(410, 30), QColor(Qt::black));
    }
};
QTEST_MAIN(TestHomeWidgets)
#include "test_homewidgets.moc"
