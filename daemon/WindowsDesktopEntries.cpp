// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "DesktopEntries.h"
#include "InitialPages.h"

#include <QCryptographicHash>
#include <QDebug>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImage>
#include <QSaveFile>
#include <QSet>
#include <QSettings>
#include <QRegularExpression>
#include <array>
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <wrl/client.h>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>
#include <algorithm>

namespace galleond {
namespace {
constexpr int MaxApps = 4096;
class ComScope final {
public:
    ComScope() = default;
    ~ComScope() { if (SUCCEEDED(m_status)) CoUninitialize(); }
    ComScope(const ComScope &) = delete;
    ComScope &operator=(const ComScope &) = delete;
    ComScope(ComScope &&) = delete;
    ComScope &operator=(ComScope &&) = delete;
private:
    HRESULT m_status = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
};

QString shortcutTarget(const QString &file)
{
    const ComScope apartment;
    Microsoft::WRL::ComPtr<IShellLinkW> link;
    if (FAILED(CoCreateInstance(CLSID_ShellLink, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(link.GetAddressOf()))))
        return {};
    Microsoft::WRL::ComPtr<IPersistFile> persist;
    if (FAILED(link.As(&persist)) || FAILED(persist->Load(file.toStdWString().c_str(), STGM_READ)))
        return {};
    std::array<wchar_t, 32768> target{};
    if (FAILED(link->GetPath(target.data(), static_cast<int>(target.size()), nullptr, SLGP_RAWPATH)))
        return {};
    std::array<wchar_t, 32768> expanded{};
    const DWORD length = ExpandEnvironmentStringsW(target.data(), expanded.data(), static_cast<DWORD>(expanded.size()));
    if (length == 0 || length > expanded.size())
        return {};
    return QDir::fromNativeSeparators(QString::fromWCharArray(expanded.data()));
}

QString cachedIcon(const QString &file)
{
    const ComScope apartment;
    const QFileInfo info(file);
    const QByteArray identity = file.toUtf8() + QByteArray::number(info.lastModified().toMSecsSinceEpoch());
    const QString root = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/galleoncore/application-icons");
    const QString output = root + QLatin1Char('/') + QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex()) + QStringLiteral(".png");
    if (QFileInfo::exists(output))
        return output;
    SHFILEINFOW shellInfo{};
    if (!SHGetFileInfoW(file.toStdWString().c_str(), 0, &shellInfo, static_cast<UINT>(sizeof(shellInfo)), SHGFI_ICON | SHGFI_LARGEICON)) {
        qWarning() << "galleond: Shell icon lookup failed" << file << GetLastError();
        return {};
    }
    const QImage image = QImage::fromHICON(shellInfo.hIcon);
    DestroyIcon(shellInfo.hIcon);
    if (image.isNull() || !QDir().mkpath(root)) {
        qWarning() << "galleond: Shell icon conversion/cache failed" << file << image.size() << root << GetLastError();
        return {};
    }
    QSaveFile saved(output);
    if (!saved.open(QIODevice::WriteOnly) || !image.save(&saved, "PNG") || !saved.commit()) {
        qWarning() << "galleond: icon cache write failed" << output << saved.errorString();
        return {};
    }
    return output;
}

bool usefulShortcut(const QString &name, const QString &target)
{
    const QString executable = QFileInfo(target).fileName().toLower();
    if (target.isEmpty() || target.startsWith(QStringLiteral("//")) || !QFileInfo(target).isAbsolute()
        || !target.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive) || !QFileInfo(target).isFile())
        return false;
    if (executable == QStringLiteral("wsl.exe") || executable == QStringLiteral("wslg.exe")
        || executable.startsWith(QStringLiteral("unins")) || executable.contains(QStringLiteral("uninstall")))
        return false;
    static const QRegularExpression unwanted(QStringLiteral("^(uninstall|désinstaller|desinstaller|remove|help|aide)(?:\\b|$)"), QRegularExpression::CaseInsensitiveOption);
    return !unwanted.match(name).hasMatch();
}

QStringList startMenuRoots()
{
    QStringList result;
    for (const QString &location : QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation)) {
        if (QFileInfo(location).isDir())
            result << QDir::cleanPath(location);
    }
    for (const KNOWNFOLDERID *id : {&FOLDERID_Programs, &FOLDERID_CommonPrograms, &FOLDERID_Desktop, &FOLDERID_PublicDesktop}) {
        PWSTR path = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(*id, KF_FLAG_DEFAULT, nullptr, &path))) {
            result << QDir::fromNativeSeparators(QString::fromWCharArray(path));
            CoTaskMemFree(path);
        }
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
    // Never resolve network targets or remote shortcut icon handlers during discovery.
    if (QDir::fromNativeSeparators(file).startsWith(QStringLiteral("//")))
        return {};
    const QFileInfo info(file);
    if (!allowedFile(info))
        return {};
    DesktopApp app;
    app.file = info.absoluteFilePath();
    app.id = id;
    app.name = info.completeBaseName();
    const QString target = info.suffix().compare(QStringLiteral("lnk"), Qt::CaseInsensitive) == 0 ? shortcutTarget(file) : app.file;
    if (!usefulShortcut(app.name, target))
        return {};
    app.iconPath = cachedIcon(target);
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
    // App Paths includes per-user and machine installations without a Start Menu shortcut (Chrome, etc.).
    for (const QString &hive : {QStringLiteral("HKEY_CURRENT_USER"), QStringLiteral("HKEY_LOCAL_MACHINE")}) {
        for (const QSettings::Format format : {QSettings::NativeFormat, QSettings::Registry32Format}) {
            QSettings registered(hive + QStringLiteral("\\Software\\Microsoft\\Windows\\CurrentVersion\\App Paths"), format);
            for (const QString &group : registered.childGroups()) {
                if (apps.size() >= MaxApps)
                    break;
                QString path = registered.value(group + QStringLiteral("/.")).toString().trimmed();
                if (path.startsWith(QLatin1Char('"')) && path.endsWith(QLatin1Char('"')))
                    path = path.mid(1, path.size() - 2);
                DesktopApp app = read(path, path);
                if (app.isValid())
                    apps.append(std::move(app));
            }
        }
    }
    QSet<QString> seen;
    apps.removeIf([&seen](const DesktopApp &app) {
        const QString target = app.file.endsWith(QStringLiteral(".lnk"), Qt::CaseInsensitive) ? shortcutTarget(app.file) : app.file;
        const QString identity = target.toCaseFolded();
        if (seen.contains(identity))
            return true;
        seen.insert(identity);
        return false;
    });
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
