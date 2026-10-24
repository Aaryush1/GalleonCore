// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <QObject>
#include <QTimer>
#include <QVariantList>
#include <QWebSocket>
#include <QHash>
#include <QSet>
#include <functional>
namespace galleond {
class RecordingController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString choice READ choice NOTIFY changed)
    Q_PROPERTY(QString backend READ backend NOTIFY changed)
    Q_PROPERTY(QVariantList options READ options NOTIFY changed)
    Q_PROPERTY(QVariantList buttons READ buttons NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(int port READ port NOTIFY changed)
public:
    explicit RecordingController(QObject *parent = nullptr, QString preferences = {});
    ~RecordingController() override;
    [[nodiscard]] QString choice() const { return m_choice; }
    [[nodiscard]] QString backend() const;
    [[nodiscard]] QVariantList options() const;
    [[nodiscard]] QVariantList buttons() const;
    [[nodiscard]] QString status() const;
    [[nodiscard]] int port() const { return m_port; }
    Q_INVOKABLE bool configure(const QString &choice, int port, const QString &password = {}, bool replacePassword = false);
    Q_INVOKABLE void refresh();
    void trigger(int index);
    // Replaces OS discovery and key sending in tests; never sends a real shortcut.
    void setPlatformForTest(bool windows, bool gameBar, bool obs, std::function<bool(int)> sender);
Q_SIGNALS:
    void changed();
private:
    void load();
    void connectObs();
    void receive(const QString &message);
    void request(const QString &type);
    void clearObs();
    QString m_preferences, m_choice = QStringLiteral("auto"), m_password, m_message, m_barMessage;
    int m_port = 4455;
    bool m_windows = false, m_gameBar = false, m_obs = false, m_ready = false, m_test = false;
    bool m_recordKnown = false, m_record = false, m_paused = false, m_replayKnown = false, m_replay = false;
    QWebSocket m_socket;
    QTimer m_timer, m_deadline;
    QSet<QString> m_requests;
    QHash<QString, QString> m_pending;
    std::function<bool(int)> m_shortcut;
};
}
