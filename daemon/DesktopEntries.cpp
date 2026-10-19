// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DesktopEntries.h"

#include <QDebug>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace galleond {

namespace {

const QRegularExpression IdPattern(QStringLiteral("^[A-Za-z0-9._-]{1,150}\\.desktop$"));
const QRegularExpression IconPattern(QStringLiteral("^[A-Za-z0-9._-]{1,100}$"));
constexpr qint64 MaxDesktopFileBytes = 64 * 1024;
constexpr int MaxListed = 2000;

QStringList applicationFolders()
{
    return QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("applications"),
                                     QStandardPaths::LocateDirectory);
}

} // namespace

bool DesktopEntries::isValidReference(const QString &desktop)
{
    if (desktop.size() > 200)
        return false;
    if (IdPattern.match(desktop).hasMatch())
        return true;
    // An absolute path: a .desktop file, no ".." part, no control character.
    if (!desktop.startsWith(QLatin1Char('/')) || !desktop.endsWith(QStringLiteral(".desktop")))
        return false;
    if (desktop.contains(QStringLiteral("..")))
        return false;
    return std::none_of(desktop.begin(), desktop.end(), [](QChar c) { return c.unicode() < 0x20; });
}

// The text after `key=` on the first such line of the [Desktop Entry] group, as written.
static QString rawValue(const QString &path, const QString &key)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray text = file.read(MaxDesktopFileBytes + 1);
    bool inEntry = false;
    const QByteArray prefix = key.toUtf8() + '=';
    for (const QByteArray &rawLine : text.split('\n')) {
        const QByteArray line = rawLine.trimmed();
        if (line.startsWith('[')) {
            inEntry = line == "[Desktop Entry]";
        } else if (inEntry && line.startsWith(prefix)) {
            return QString::fromUtf8(line.mid(prefix.size())).left(2000);
        }
    }
    return {};
}

DesktopApp DesktopEntries::read(const QString &file, const QString &id)
{
    const QFileInfo info(file);
    if (!info.isFile() || info.size() <= 0 || info.size() > MaxDesktopFileBytes)
        return {};

    QSettings settings(info.absoluteFilePath(), QSettings::IniFormat);
    settings.beginGroup(QStringLiteral("Desktop Entry"));
    if (settings.value(QStringLiteral("Type")).toString() != QLatin1String("Application")
        || settings.value(QStringLiteral("Exec")).toString().trimmed().isEmpty())
        return {};

    DesktopApp app;
    app.file = info.absoluteFilePath();
    app.id = id.isEmpty() ? info.fileName() : id;
    // Categories and Exec are read from the file itself: QSettings cuts a value at its first semicolon.
    for (const QString &category : rawValue(info.absoluteFilePath(), QStringLiteral("Categories")).split(QLatin1Char(';'), Qt::SkipEmptyParts)) {
        if (app.categories.size() < 16 && category.trimmed().size() <= 40)
            app.categories.append(category.trimmed());
    }
    static const QRegularExpression steamGame(QStringLiteral("steam://rungameid/(\\d{1,10})"));
    const QRegularExpressionMatch steamMatch = steamGame.match(rawValue(info.absoluteFilePath(), QStringLiteral("Exec")));
    if (steamMatch.hasMatch()) {
        const qint64 appid = steamMatch.captured(1).toLongLong();
        if (appid > 0 && appid <= 2'000'000'000)
            app.steamAppId = static_cast<int>(appid);
    }

    // The name in the user's language, as the desktop would show it.
    const QLocale locale;
    for (const QString &key : {QStringLiteral("Name[%1]").arg(locale.name()),
                               QStringLiteral("Name[%1]").arg(locale.name().section(QLatin1Char('_'), 0, 0))}) {
        const QString localized = settings.value(key).toString().simplified();
        if (!localized.isEmpty()) {
            app.name = localized.left(120);
            break;
        }
    }
    if (app.name.isEmpty())
        app.name = settings.value(QStringLiteral("Name")).toString().simplified().left(120);
    if (app.name.isEmpty())
        app.name = info.completeBaseName();

    const QString icon = settings.value(QStringLiteral("Icon")).toString().trimmed();
    if (IconPattern.match(icon).hasMatch()) {
        app.icon = icon; // a name of the icon theme
    } else if (icon.startsWith(QLatin1Char('/')) && icon.size() <= 400 && !icon.contains(QStringLiteral(".."))
               && std::none_of(icon.begin(), icon.end(), [](QChar c) { return c.unicode() < 0x20; })) {
        // An image given by path (Snap packages do this). It is only ever
        // decoded as an image, with the same checks as any picture.
        const QString suffix = QFileInfo(icon).suffix().toLower();
        if (suffix == QLatin1String("png") || suffix == QLatin1String("svg") || suffix == QLatin1String("jpg")
            || suffix == QLatin1String("jpeg"))
            app.iconPath = icon;
    }
    return app;
}

DesktopApp DesktopEntries::lookup(const QString &desktop)
{
    if (!isValidReference(desktop))
        return {};
    if (desktop.startsWith(QLatin1Char('/')))
        return read(desktop, QString());

    // The ID is the file name, in the applications folder or in a sub-folder:
    // "kde-app.desktop" may be applications/kde/app.desktop.
    for (const QString &folder : applicationFolders()) {
        QString relative = desktop;
        for (;;) {
            const QString candidate = QDir(folder).filePath(relative);
            if (QFileInfo(candidate).isFile())
                return read(candidate, desktop);
            const qsizetype dash = relative.indexOf(QLatin1Char('-'));
            if (dash < 0)
                break;
            relative[dash] = QLatin1Char('/');
        }
    }
    return {};
}

QList<DesktopApp> DesktopEntries::list()
{
    QList<DesktopApp> apps;
    QStringList seen;
    // The first folder wins, as in the desktop: the user's file hides the system's.
    for (const QString &folder : applicationFolders()) {
        QDirIterator it(folder, {QStringLiteral("*.desktop")}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext() && apps.size() < MaxListed) {
            const QString path = it.next();
            QString id = QDir(folder).relativeFilePath(path);
            id.replace(QLatin1Char('/'), QLatin1Char('-'));
            if (seen.contains(id))
                continue;
            seen.append(id);
            QSettings settings(path, QSettings::IniFormat);
            settings.beginGroup(QStringLiteral("Desktop Entry"));
            if (settings.value(QStringLiteral("NoDisplay")).toBool() || settings.value(QStringLiteral("Hidden")).toBool())
                continue;
            if (DesktopApp app = read(path, id); app.isValid()) {
                // An ID with a space or another odd character cannot be written in
                // a pages file: such an application is named by its file instead.
                if (!IdPattern.match(app.id).hasMatch() && isValidReference(app.file))
                    app.id = app.file;
                apps.append(app);
            }
        }
    }
    std::sort(apps.begin(), apps.end(), [](const DesktopApp &a, const DesktopApp &b) {
        return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
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
    if (!app.isValid())
        return false;
    // gio launch understands Exec, field codes, Terminal=true and DBusActivatable.
    const QString gio = QStandardPaths::findExecutable(QStringLiteral("gio"));
    if (!gio.isEmpty())
        return QProcess::startDetached(gio, {QStringLiteral("launch"), app.file});
    const QString gtk = QStandardPaths::findExecutable(QStringLiteral("gtk-launch"));
    if (!gtk.isEmpty() && IdPattern.match(app.id).hasMatch())
        return QProcess::startDetached(gtk, {app.id});
    for (const QString &name : {QStringLiteral("kioclient6"), QStringLiteral("kioclient5"), QStringLiteral("kioclient")}) {
        const QString kio = QStandardPaths::findExecutable(name);
        if (!kio.isEmpty())
            return QProcess::startDetached(kio, {QStringLiteral("exec"), app.file});
    }
    qWarning() << "galleond: no launcher found (gio, gtk-launch or kioclient)";
    return false;
}

} // namespace galleond
