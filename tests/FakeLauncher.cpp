// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include <cstdio>
#include <memory>
int main(int argc, char *argv[])
{
    const auto app = std::make_unique<QCoreApplication>(argc, argv);
    const QString key = app->arguments().value(2);
    if (key == QStringLiteral("slow")) {
        QTimer::singleShot(5000, app.get(), &QCoreApplication::quit);
        return QCoreApplication::exec();
    }
    QFile output;
    if (!output.open(key == QStringLiteral("error-flood") ? stderr : stdout, QIODevice::WriteOnly))
        return 1;
    if (key == QStringLiteral("flood") || key == QStringLiteral("error-flood"))
        output.write(QByteArray(qsizetype{128} * 1024, 'x'));
    else
        output.write(QJsonDocument(QJsonObject{{QStringLiteral("name"), key}, {QStringLiteral("icon"), QString()}}).toJson());
    return output.flush() ? 0 : 1;
}
