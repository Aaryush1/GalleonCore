// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "UpdateRelease.h"
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QSettings>
#include <QTimer>

namespace bosun {
class UpdateChecker : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString installedVersion READ installedVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString lastChecked READ lastChecked NOTIFY changed)
    Q_PROPERTY(QString downloadLabel READ downloadLabel NOTIFY changed)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool automatic READ automatic NOTIFY changed)
    Q_PROPERTY(bool packageManaged READ packageManaged NOTIFY changed)
public:
    struct Options {
        QString installedVersion;
        QString settingsFile;
        UpdatePlatform platform;
        int timeoutMs = 15000;
        bool packageManaged = false;
    };
    // An injected manager is borrowed, must outlive this object, and is only
    // used by tests. Construction itself never performs a network request.
    explicit UpdateChecker(Options options, QObject *parent = nullptr, QNetworkAccessManager *manager = nullptr);
    ~UpdateChecker() override;
    [[nodiscard]] QString installedVersion() const { return m_options.installedVersion; }
    [[nodiscard]] QString latestVersion() const { return m_release ? m_release->version : QString(); }
    [[nodiscard]] QString status() const { return m_status; }
    [[nodiscard]] QString lastChecked() const;
    [[nodiscard]] QString downloadLabel() const;
    [[nodiscard]] bool available() const;
    [[nodiscard]] bool busy() const { return m_reply != nullptr; }
    [[nodiscard]] bool automatic() const { return m_automatic; }
    [[nodiscard]] bool packageManaged() const;
    void startAutomaticChecks();
    void setRepositoryManaged(bool managed, bool known);
    Q_INVOKABLE void checkNow();
    Q_INVOKABLE void checkAutomatically();
    Q_INVOKABLE void setAutomatic(bool enabled);
    Q_INVOKABLE void openDownload();
    Q_INVOKABLE void openReleaseNotes();
    Q_INVOKABLE void openWebsite();
Q_SIGNALS:
    void changed();
    // Main connects this to QDesktopServices. Tests never open a real browser.
    void openUrlRequested(const QUrl &url);
private:
    void check(bool automaticRequest);
    void consume();
    void finish();
    void cancel();
    void fail(const QString &message);
    Options m_options;
    QSettings m_settings;
    QNetworkAccessManager m_network;
    QNetworkAccessManager *m_manager;
    QNetworkReply *m_reply = nullptr; // owned by the manager; cancelled before manager destruction
    QTimer m_timeout;
    QTimer m_periodic;
    QByteArray m_body;
    std::optional<UpdateRelease> m_release;
    QString m_status;
    qint64 m_lastChecked = 0;
    qint64 m_lastAttempt = 0;
    qint64 m_retryAfter = 0;
    bool m_automatic = true;
    bool m_automaticRequest = false;
};
} // namespace bosun
