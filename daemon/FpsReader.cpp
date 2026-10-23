// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "FpsReader.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QRegularExpression>
#include <QStringList>
#include <cmath>
namespace galleond {
namespace {
QStringList csv(const QByteArray &line) {
    QStringList fields;
    QString field;
    bool quoted = false;
    const auto text = QString::fromUtf8(line).trimmed();
    for (qsizetype i = 0; i < text.size(); ++i) {
        const auto c = text[i];
        if (c == QChar('"')) {
            if (quoted && i + 1 < text.size() && text[i + 1] == QChar('"')) { field += c; ++i; }
            else quoted = !quoted;
        } else if (c == QChar(',') && !quoted) { fields.append(field.trimmed()); field.clear(); }
        else field += c;
    }
    if (quoted) return {};
    fields.append(field.trimmed());
    return fields;
}
QString message(const char *text) { return QCoreApplication::translate("FpsReader", text); }
}
FpsSample readFpsLog(const QString &source, const QString &process, const QDateTime &now) {
    if (source.isEmpty()) return {{}, message("Configure FPS source")};
    QFileInfo info(source);
    if (info.isDir()) {
        const auto files = QDir(source).entryInfoList({QStringLiteral("*.csv")}, QDir::Files | QDir::NoSymLinks, QDir::Time);
        bool found = false;
        for (const auto &file : files) {
            if (file.fileName().endsWith(QStringLiteral("_summary.csv"))) continue;
            info = file; found = true; break;
        }
        if (!found) return {{}, message("Waiting for game capture")};
    }
    if (!info.isFile() || info.lastModified().msecsTo(now) > 10000 || info.lastModified().msecsTo(now) < -10000)
        return {{}, message("Waiting for game capture")};
    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) return {{}, message("FPS log unreadable")};
    const auto head = file.read(16384);
    QStringList header;
    int fpsIndex = -1, intervalIndex = -1, appIndex = -1, swapIndex = -1;
    for (const auto &line : head.split('\n')) {
        auto columns = csv(line);
        for (auto &column : columns) { column.remove(QChar(0xfeff)); column = column.toLower(); }
        const int fps = int(columns.indexOf(QStringLiteral("fps")));
        const int interval = int(columns.indexOf(QStringLiteral("msbetweenpresents")));
        if (fps < 0 && interval < 0) continue;
        header = columns; fpsIndex = fps; intervalIndex = interval;
        appIndex = int(columns.indexOf(QStringLiteral("application")));
        swapIndex = int(columns.indexOf(QStringLiteral("swapchainaddress")));
        break;
    }
    if (header.isEmpty()) return {{}, message("Unsupported FPS log")};
    const qint64 offset = qMax(qint64(0), file.size() - 65536);
    if (!file.seek(offset)) return {{}, message("FPS log unreadable")};
    auto lines = file.read(65536).split('\n');
    if (offset > 0 && !lines.isEmpty()) lines.removeFirst();
    // The final line may still be in flight. Only consume complete records.
    if (!lines.isEmpty()) lines.removeLast();
    QSet<QString> applications;
    for (const auto &line : lines) {
        const auto fields = csv(line);
        if (fields.size() != header.size() || appIndex < 0) continue;
        bool ok = false; fields.value(intervalIndex).toDouble(&ok);
        if (ok) applications.insert(fields.value(appIndex).toLower());
    }
    if (process.isEmpty() && applications.size() > 1) return {{}, message("Select game executable")};
    const auto makeSample = [&](double fps, const QStringList &fields, const QByteArray &row) {
        FpsSample result{fps, {}};
        result.process = appIndex >= 0 ? fields.value(appIndex) : info.completeBaseName();
        result.process.remove(QRegularExpression(QStringLiteral("_[0-9]{4}-[0-9]{2}-[0-9]{2}.*$")));
        for (const auto &key : {QStringLiteral("cpu_temp"), QStringLiteral("gpu_temp")}) {
            bool ok = false; const double value = fields.value(header.indexOf(key)).toDouble(&ok);
            if (ok && std::isfinite(value) && value > 0 && value < 150) {
                if (key == "cpu_temp") result.cpuTemperature = value;
                else result.gpuTemperature = value;
            }
        }
        for (const auto &key : {QStringLiteral("cpu_load"), QStringLiteral("gpu_load")}) {
            bool ok = false; const double value = fields.value(header.indexOf(key)).toDouble(&ok);
            if (ok && std::isfinite(value) && value >= 0 && value <= 100) {
                if (key == "cpu_load") result.cpuLoad = value;
                else result.gpuLoad = value;
            }
        }
        // MangoHud may know the GPU name without access to its telemetry (e.g.
        // Steam runtime/NVML): zero load, temperature, clock and VRAM are then
        // placeholders. Do not let that load mask a working native provider.
        if (result.gpuLoad && *result.gpuLoad == 0) {
            int indicators = 0;
            bool allZero = true;
            for (const auto &key : {QStringLiteral("gpu_temp"), QStringLiteral("gpu_core_clock"), QStringLiteral("gpu_vram_used")}) {
                const auto index = header.indexOf(key);
                if (index < 0) continue;
                bool ok = false; const auto value = fields.value(index).toDouble(&ok);
                if (!ok || !std::isfinite(value) || value != 0) allZero = false;
                ++indicators;
            }
            if (indicators >= 2 && allZero) result.gpuLoad.reset();
        }
        result.token = info.absoluteFilePath() + QString::fromUtf8(row);
        return result;
    };
    QString swap;
    QStringList lastFields;
    QByteArray lastRow;
    double intervals = 0;
    int count = 0;
    for (auto i = lines.crbegin(); i != lines.crend(); ++i) {
        const auto fields = csv(*i);
        if (fields.size() != header.size()) continue;
        if (!process.isEmpty() && appIndex >= 0 && fields.value(appIndex).compare(process, Qt::CaseInsensitive) != 0) continue;
        if (swapIndex >= 0) {
            if (swap.isEmpty()) swap = fields.value(swapIndex);
            if (swap != fields.value(swapIndex)) continue;
        }
        if (intervalIndex >= 0 && fields.value(intervalIndex).compare(QStringLiteral("MsBetweenPresents"), Qt::CaseInsensitive) == 0) break;
        bool ok = false;
        const double value = fields.value(fpsIndex >= 0 ? fpsIndex : intervalIndex).toDouble(&ok);
        if (!ok || !std::isfinite(value) || value < 0 || (fpsIndex >= 0 && value > 10000))
            return {{}, message("Waiting for valid FPS")};
        if (lastFields.isEmpty()) { lastFields = fields; lastRow = *i; }
        if (fpsIndex >= 0) return makeSample(value, fields, *i);
        if (value <= 0) continue;
        intervals += value; ++count;
        if (count >= 120 || intervals >= 1000) break;
    }
    if (count && intervals > 0) {
        const double fps = 1000.0 * count / intervals;
        if (fps <= 10000) return makeSample(fps, lastFields, lastRow);
    }
    return {{}, message("Waiting for game capture")};
}
}
