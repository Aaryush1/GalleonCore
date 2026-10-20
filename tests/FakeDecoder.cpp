// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
// A decoder stand-in: ignores FFmpeg flags except scale and never exits on its own.
#include <QCoreApplication>
#include <QFile>
#include <QRegularExpression>
#include <QTimer>
#include <cstdio>
#include <memory>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif
int main(int argc, char *argv[])
{
    const auto app = std::make_unique<QCoreApplication>(argc, argv);
#ifdef Q_OS_WIN
    static_cast<void>(_setmode(_fileno(stdout), _O_BINARY));
#endif
    const QRegularExpression scale(QStringLiteral("scale=([0-9]+):([0-9]+)"));
    int width = 0;
    int height = 0;
    for (const QString &argument : QCoreApplication::arguments()) {
        const auto match = scale.match(argument);
        if (match.hasMatch()) {
            width = match.captured(1).toInt();
            height = match.captured(2).toInt();
        }
    }
    if (width <= 0 || height <= 0 || width > 2048 || height > 4096)
        return 1;
    const QByteArray frame(width * height * 3, '\0');
    QFile output;
    if (!output.open(stdout, QIODevice::WriteOnly))
        return 1;
    QTimer frames;
    QObject::connect(&frames, &QTimer::timeout, app.get(), [&] {
        if (output.write(frame) != frame.size() || !output.flush())
            QCoreApplication::quit();
    });
    frames.start(50);
    return QCoreApplication::exec();
}
