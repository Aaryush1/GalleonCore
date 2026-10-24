// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "FpsReader.h"
#include <QObject>
#include <QProcess>
#include <QVariantMap>
namespace galleond {
struct CaptureTarget {
    quint32 pid = 0;
    QString name;
    bool operator==(const CaptureTarget &) const = default;
};
// Owns only its child capture. Never stops another PresentMon session or game.
class GameCapture : public QObject {
    Q_OBJECT
public:
    explicit GameCapture(QString output, QObject *parent = nullptr);
    ~GameCapture() override;
    void tick(bool enabled, const CaptureTarget &target, const QString &program, const QString &apiPath = {});
    FpsSample sample();
    void retry() { m_retryAt = 0; }
    static QStringList registeredPresentMonFolders();
    [[nodiscard]] QVariantMap status() const;
    static QString findPresentMon(const QString &overridePath = {}, const QStringList &roots = {});
    static bool isGamePath(QString path, const QStringList &profiles = {});
    static CaptureTarget detectGame(const QStringList &profiles, quint32 currentPid = 0);
private:
    void consume();
    void stop();
    QProcess m_process, m_cleanup;
    QString m_output, m_program, m_apiPath, m_state = QStringLiteral("waiting"), m_message;
    CaptureTarget m_target;
    QByteArray m_header, m_tail, m_partial, m_diagnostics;
    qint64 m_retryAt = 0, m_startedAt = 0;
    bool m_stopping = false, m_ownsOutput = false;
};
}
