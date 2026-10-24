// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include "GameMode.h"
#include "GameCapture.h"
#include <QTimer>
#include <QVariantList>
#include <QVariantMap>
#include <QNetworkAccessManager>
#include <QProcess>
namespace galleond {
// Shared by Bosun and the daemon; an atomic file transfers edits across processes.
class HomeWidgets : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList layout READ layout NOTIFY changed)
    Q_PROPERTY(QVariantMap values READ values NOTIFY changed)
    Q_PROPERTY(QVariantMap location READ location NOTIFY changed)
    Q_PROPERTY(QString recommendedFpsFolder READ recommendedFpsFolder CONSTANT)
    Q_PROPERTY(QString steamLaunchOptions READ steamLaunchOptions CONSTANT)
    Q_PROPERTY(QVariantMap fpsConfig READ fpsConfig NOTIFY changed)
    Q_PROPERTY(QVariantMap gameState READ gameState NOTIFY changed)
    Q_PROPERTY(QVariantMap gameConfig READ gameConfig NOTIFY changed)
    Q_PROPERTY(QVariantMap captureStatus READ captureStatus NOTIFY changed)
    Q_PROPERTY(bool windowsCapture READ windowsCapture CONSTANT)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit HomeWidgets(QObject *parent = nullptr, QString file = {}, bool draftMode = false);
    QVariantList layout() const { return m_layout; }
    QVariantMap values() const {
        auto result = m_values;
        const auto city = m_location.value(QStringLiteral("city")).toString().trimmed().left(80);
        if (!city.isEmpty()) result.insert(QStringLiteral("weatherCity"), city);
        return result;
    }
    QVariantMap location() const { return m_location; }
    [[nodiscard]] QString recommendedFpsFolder() const;
    [[nodiscard]] QString steamLaunchOptions() const;
    Q_INVOKABLE bool prepareFpsFolder();
    Q_INVOKABLE bool configureMangoHud();
    QVariantMap fpsConfig() const { return m_fpsConfig; }
    QVariantMap gameState() const { return m_gameState; }
    QVariantMap gameConfig() const { return m_gameConfig; }
    QVariantMap captureStatus() const { return m_captureStatus; }
    bool windowsCapture() const {
#ifdef Q_OS_WIN
        return true;
#else
        return false;
#endif
    }
    bool pending() const { return m_pending; }
    QString error() const { return m_error; }
    Q_INVOKABLE bool setWidget(const QString &id, bool enabled, int x, int y, int width, int height);
    Q_INVOKABLE bool setLocation(const QString &latitude, const QString &longitude);
    Q_INVOKABLE bool setWidgetTextSize(const QString &id, int pixels);
    Q_INVOKABLE bool setFpsSource(const QString &source, const QString &process);
    Q_INVOKABLE bool setCity(const QString &city);
    Q_INVOKABLE bool setGameOptions(bool automatic, bool localSensors);
    Q_INVOKABLE bool requestGameCapture(bool start = true);
    Q_INVOKABLE bool setCaptureOptions(bool managed, const QString &program);
    Q_INVOKABLE bool setGameProfile(const QString &process, const QString &title, const QString &image);
    Q_INVOKABLE bool reset();
    Q_INVOKABLE bool apply();
Q_SIGNALS:
    void changed();
private:
    void refresh();
    void load();
    bool save();
    bool write();
    void weather();
    void pollGameSensors();
    void pollGpu();
    QProcess m_gpuProcess;
    qint64 m_gpuAt = 0;
    int m_gpuSerial = 0;
    bool m_gpuUsesProbe = false;
    qint64 m_nativeGpuRetryAt = 0;
    QString m_gpuProvider;
    std::optional<double> m_gpuTemperature;
    std::optional<double> m_gpuLoad;
    GameMode m_gameMode;
    GameCapture m_capture;
    CaptureTarget m_captureTarget;
    quint32 m_captureStoppedPid = 0;
    QVariantMap m_captureInstallation;
    QString m_captureProgram, m_capturePathOption;
    qint64 m_captureDiscoveryAt = 0;
    QVariantMap m_captureStatus;
    QVariantMap m_gameConfig, m_gameState, m_gameSensors;
    bool m_sensorPending = false;
    qint64 m_sensorAt = 0;
    bool m_draftMode = false, m_pending = false;
    QString m_file, m_error;
    QVariantList m_layout;
    QVariantMap m_values;
    QVariantMap m_location;
    QVariantMap m_fpsConfig;
    QByteArray m_lastFile, m_lastCaptureRequest;
    QTimer m_timer;
    QNetworkAccessManager m_network;
    bool m_weatherPending = false;
    qint64 m_weatherAt = 0;
    quint64 m_cpuTotal = 0, m_cpuIdle = 0;
};
}
