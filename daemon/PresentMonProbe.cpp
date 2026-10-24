// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later
#include "PresentMonService.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QTimer>
#ifdef Q_OS_WIN
#include <windows.h>
#endif
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto args = QCoreApplication::arguments();
    const auto argument = [&](const QString &key) { const auto index = args.indexOf(key); return index < 0 ? QString() : args.value(index + 1); };
    QFile output;
    QFile diagnostic;
    if (!output.open(stdout, QIODevice::WriteOnly) || !diagnostic.open(stderr, QIODevice::WriteOnly)) return 1;
    const auto report = [&](const QString &error) { diagnostic.write(error.toUtf8() + '\n'); diagnostic.flush(); };
    const auto serviceError = galleond::startPresentMonService();
    if (!serviceError.isEmpty()) { report(serviceError); return 2; }
    galleond::PresentMonSession session;
    const auto pid = argument(QStringLiteral("--pid")).toUInt();
    if (!session.start(argument(QStringLiteral("--api")), pid)) { report(session.error()); return 3; }
    const auto name = argument(QStringLiteral("--name")).replace('"', "\"\"");
    output.write("Application,fps,elapsed\n"); output.flush();
    QElapsedTimer elapsed; elapsed.start();
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
#ifdef Q_OS_WIN
        DWORD available = 0;
        if (PeekNamedPipe(GetStdHandle(STD_INPUT_HANDLE), nullptr, 0, nullptr, &available, nullptr) && available) { QCoreApplication::quit(); return; }
        const auto process = OpenProcess(SYNCHRONIZE, FALSE, pid);
        if (!process) { QCoreApplication::quit(); return; }
        const bool exited = WaitForSingleObject(process, 0) == WAIT_OBJECT_0; CloseHandle(process);
        if (exited) { QCoreApplication::quit(); return; }
#endif
        const auto fps = session.poll();
        if (!session.error().isEmpty()) { report(session.error()); QCoreApplication::exit(4); return; }
        if (fps) { output.write('"' + name.toUtf8() + "\"," + QByteArray::number(*fps, 'f', 3) + ',' + QByteArray::number(elapsed.elapsed()) + '\n'); output.flush(); }
    });
    timer.start(250);
    return QCoreApplication::exec();
}
