// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "GpuSensors.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    auto sensors = galleond::readNvmlSensors();
#ifndef Q_OS_WIN
    if (sensors.isEmpty()) sensors = galleond::readAmdSmiSensors();
#endif
    QFile output;
    if (!output.open(stdout, QIODevice::WriteOnly)) return 1;
    output.write(QJsonDocument(QJsonObject::fromVariantMap(sensors)).toJson(QJsonDocument::Compact));
    return output.flush() ? 0 : 1;
}
