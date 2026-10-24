// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include <QCoreApplication>
#include <QFile>
#include <QTimer>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.contains(QStringLiteral("--terminate_existing_session"))) return 0;
    const auto pid = args.value(args.indexOf(QStringLiteral("--process_id")) + 1);
    QFile output;
    if (!output.open(stdout, QIODevice::WriteOnly)) return 1;
    if (pid == QStringLiteral("99")) return 5;
    output.write("Application,ProcessID,SwapChainAddress,msBetweenPresents,TimeInSeconds\n");
    output.flush();
    if (pid == QStringLiteral("13")) {
        for (int i = 0; i < 10000; ++i) output.write("fixture.exe,13,0x1,10," + QByteArray::number(i) + "\n");
        output.flush();
    }
    int frames = 0;
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        output.write("fixture.exe," + pid.toUtf8() + ",0x1,10," + QByteArray::number(++frames) + "\n");
        output.flush();
    });
    timer.start(5);
    return app.exec();
}
