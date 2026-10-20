// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DesktopEntries.h"
#include "InitialPages.h"

#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>

namespace galleond {
namespace {
constexpr int MaxApps = 4096;

QStringList startMenuRoots()
{
    QStringList result;
    for (const QString &location : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        if (QFileInfo(location).isDir())
            result << QDir::cleanPath(location);
    }
    result.removeDuplicates();
    return result;
}

bool allowedFile(const QFileInfo &info)
{
    const QString suffix = info.suffix().toLower();
    return info.isAbsolute() && info.isFile() && !info.isSymbolicLink()
        && (suffix == QStringLiteral("lnk") || suffix == QStringLiteral("exe"));
}
}

bool DesktopEntries::isValidReference(const QString &desktop)
{
    if (desktop.isEmpty() || desktop.size() > 4096 || desktop.contains(QChar::Null))
        return false;
    const QFileInfo info(desktop);
    const QString suffix = info.suffix().toLower();
    return info.isAbsolute() && (suffix == QStringLiteral("lnk") || suffix == QStringLiteral("exe"));
}

DesktopApp DesktopEntries::read(const QString &file, const QString &id)
{
    const QFileInfo info(file);
    if (!allowedFile(info))
        return {};
    DesktopApp app;
    app.file = info.absoluteFilePath();
    app.id = id;
    app.name = info.completeBaseName();
    app.categories = windowsAppCategories(app.name, info.absolutePath());
    return app;
}

DesktopApp DesktopEntries::lookup(const QString &desktop)
{
    if (!isValidReference(desktop))
        return {};
    return read(desktop, desktop);
}

QList<DesktopApp> DesktopEntries::list()
{
    QList<DesktopApp> apps;
    for (const QString &root : startMenuRoots()) {
        QDirIterator files(root, {QStringLiteral("*.lnk")}, QDir::Files,
                           QDirIterator::Subdirectories);
        while (files.hasNext() && apps.size() < MaxApps) {
            const QString path = files.next();
            DesktopApp app = read(path, path);
            if (app.isValid())
                apps.append(std::move(app));
        }
        if (apps.size() >= MaxApps)
            break;
    }
    std::ranges::sort(apps, [](const DesktopApp &left, const DesktopApp &right) {
        return QString::localeAwareCompare(left.name, right.name) < 0;
    });
    return apps;
}

QList<DesktopApp> DesktopEntries::listCategory(const QString &category)
{
    QList<DesktopApp> apps = list();
    apps.removeIf([&category](const DesktopApp &app) { return !app.categories.contains(category); });
    return apps;
}

bool DesktopEntries::launch(const DesktopApp &app)
{
    if (!isValidReference(app.file) || !allowedFile(QFileInfo(app.file)))
        return false;
    if (QFileInfo(app.file).suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) == 0)
        return QProcess::startDetached(app.file, {});
    return QDesktopServices::openUrl(QUrl::fromLocalFile(app.file));
}
} // namespace galleond
