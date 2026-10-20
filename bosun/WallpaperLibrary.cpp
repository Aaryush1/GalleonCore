// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "WallpaperLibrary.h"

#include "CropMath.h"
#include "FileNames.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QMutexLocker>
#include <QSaveFile>
#include <QSettings>
#include <QUrl>

namespace bosun {

namespace {

constexpr int AllocationLimitMegabytes = 512;
constexpr int JpegQuality = 92;
const char *const SettingName = "wallpaper";
const char *const MineStyle = "mine";

QString words(const char *text)
{
    return QCoreApplication::translate("WallpaperLibrary", text);
}

QSettings galleondSettings(const QString &file)
{
    return file.isEmpty() ? QSettings(QStringLiteral("galleoncore"), QStringLiteral("galleond")) : QSettings(file, QSettings::IniFormat);
}

// A picture that is worth reading: a regular file, not huge, with sane dimensions.
bool worthReading(const QString &path, QSize &size)
{
    const QFileInfo info(path);
    if (!info.isFile() || info.size() <= 0 || info.size() > WallpaperLibrary::MaxFileBytes)
        return false;
    QImageReader reader(info.absoluteFilePath());
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    size = reader.size();
    return reader.canRead() && size.isValid() && size.width() <= WallpaperLibrary::MaxDimension && size.height() <= WallpaperLibrary::MaxDimension;
}

} // namespace

WallpaperLibrary::Places WallpaperLibrary::defaultPlaces(const QString &buildTree)
{
    return {.roots = galleond::WallpaperCatalog::defaultRoots({}, buildTree),
            .userRoot = galleond::WallpaperCatalog::userRoot(),
            .settingsFile = {}};
}

WallpaperLibrary::WallpaperLibrary(Places places, QObject *parent)
    : QObject(parent)
    , m_places(std::move(places))
    , m_catalog(QStringList())
{
    // Pictures are decoded in memory: no more than this for one (the limit is the program's, set once).
    QImageReader::setAllocationLimit(AllocationLimitMegabytes);
    // The user's own folder is read too, even before it exists: a picture added later shows.
    if (!m_places.userRoot.isEmpty() && !m_places.roots.contains(m_places.userRoot))
        m_places.roots.prepend(m_places.userRoot);
    m_catalog = galleond::WallpaperCatalog(m_places.roots);
    reload();
}

bool WallpaperLibrary::isMine(const QString &path) const
{
    const QString mine = QFileInfo(m_places.userRoot).canonicalFilePath();
    const QFileInfo info(path);
    return !mine.isEmpty() && !info.isSymLink() && info.canonicalFilePath().startsWith(mine + QLatin1Char('/'));
}

QString WallpaperLibrary::current() const
{
    return galleondSettings(m_places.settingsFile).value(QString::fromLatin1(SettingName)).toString();
}

void WallpaperLibrary::reload()
{
    m_catalog.rescan();
    QVariantList list;
    QHash<QString, QString> paths;
    for (const galleond::WallpaperEntry &entry : m_catalog.entries()) {
        paths.insert(entry.name, entry.path);
        list.append(QVariantMap{{QStringLiteral("name"), entry.name},
                                {QStringLiteral("style"), entry.style},
                                {QStringLiteral("mine"), isMine(entry.path)},
                                {QStringLiteral("thumbnail"), QStringLiteral("image://wallpapers/") + entry.name}});
    }
    {
        const QMutexLocker lock(&m_lock);
        m_paths = paths;
    }
    m_wallpapers = list;
    Q_EMIT changed();
}

QString WallpaperLibrary::apply(const QString &name)
{
    if (!m_catalog.find(name))
        return words("This wallpaper is not in the list.");
    QSettings settings = galleondSettings(m_places.settingsFile);
    settings.setValue(QString::fromLatin1(SettingName), name);
    settings.sync();
    // When galleond runs it shows it now (and keeps it, as above); else it will at its next start.
    const QDBusConnectionInterface *bus = QDBusConnection::sessionBus().interface();
    if (bus && bus->isServiceRegistered(QStringLiteral("org.galleoncore.Deck"))) {
        QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.galleoncore.Deck"), QStringLiteral("/org/galleoncore/Deck"),
                                                           QStringLiteral("org.galleoncore.Deck"), QStringLiteral("SetWallpaper"));
        call << name;
        QDBusConnection::sessionBus().asyncCall(call);
    }
    Q_EMIT changed();
    return {};
}

QString WallpaperLibrary::removeMine(const QString &name)
{
    const galleond::WallpaperEntry *entry = m_catalog.find(name);
    if (!entry)
        return words("This wallpaper is not in the list.");
    if (!isMine(entry->path))
        return words("Only your own pictures can be deleted here.");
    if (name == current())
        return words("This wallpaper is the one in use: choose another one first.");
    if (!QFile::remove(entry->path))
        return words("The picture could not be deleted.");
    reload();
    return {};
}

QImage WallpaperLibrary::readChecked(const QString &path, int side)
{
    QSize size;
    if (!worthReading(path, size))
        return {};
    QImageReader reader(QFileInfo(path).absoluteFilePath());
    reader.setDecideFormatFromContent(true);
    reader.setAutoTransform(true);
    if (side > 0 && (size.width() > side || size.height() > side))
        reader.setScaledSize(size.scaled(side, side, Qt::KeepAspectRatio)); // decoders that can, decode small
    QImage image = reader.read();
    if (image.isNull())
        return {};
    if (side > 0 && (image.width() > side || image.height() > side))
        image = image.scaled(side, side, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    return image;
}

QImage WallpaperLibrary::thumbnail(const QString &name) const
{
    QString path;
    {
        const QMutexLocker lock(&m_lock);
        path = m_paths.value(name);
    }
    return path.isEmpty() ? QImage() : readChecked(path, ThumbnailWidth);
}

QImage WallpaperLibrary::candidatePreview() const
{
    const QMutexLocker lock(&m_lock);
    return m_preview;
}

void WallpaperLibrary::setCandidate(const QString &chosen, const QString &why)
{
    m_candidate.clear();
    m_candidatePath.clear();
    {
        const QMutexLocker lock(&m_lock);
        m_preview = QImage();
    }
    if (chosen.isEmpty()) {
        Q_EMIT changed();
        return;
    }
    QVariantMap map{{QStringLiteral("chosen"), chosen}, {QStringLiteral("name"), QFileInfo(chosen).completeBaseName()}};
    QSize size;
    const QUrl url(chosen);
    const QString path = url.isLocalFile() ? url.toLocalFile() : chosen;
    QString problem = why;
    if (problem.isEmpty() && !worthReading(path, size))
        problem = words("This is not a picture that can be used (a JPEG, PNG or similar file, up to 32 MB and 8192 pixels).");
    const QImage preview = problem.isEmpty() ? readChecked(path, PreviewSide) : QImage();
    if (problem.isEmpty() && preview.isNull())
        problem = words("The picture could not be read.");
    if (problem.isEmpty()) {
        m_candidatePath = QFileInfo(path).absoluteFilePath();
        {
            const QMutexLocker lock(&m_lock);
            m_preview = preview;
        }
        ++m_serial;
        const QSize canvas(ScreenWidth, ScreenHeight);
        // The size is the one after the rotation the file asks for.
        QImageReader reader(m_candidatePath);
        reader.setAutoTransform(true);
        const QSize real = reader.size();
        map.insert(QStringLiteral("state"), QStringLiteral("ready"));
        map.insert(QStringLiteral("width"), real.width());
        map.insert(QStringLiteral("height"), real.height());
        map.insert(QStringLiteral("canvasWidth"), canvas.width());
        map.insert(QStringLiteral("canvasHeight"), canvas.height());
        map.insert(QStringLiteral("preview"), QStringLiteral("image://wallpaperpreview/%1").arg(m_serial));
        map.insert(QStringLiteral("suggestedName"), QFileInfo(m_candidatePath).completeBaseName());
    } else {
        map.insert(QStringLiteral("state"), QStringLiteral("unusable"));
        map.insert(QStringLiteral("why"), problem);
    }
    m_candidate = map;
    Q_EMIT changed();
}

void WallpaperLibrary::inspect(const QString &pathOrUrl)
{
    setCandidate(pathOrUrl.trimmed(), {});
}

void WallpaperLibrary::clearCandidate()
{
    setCandidate({}, {});
}

QString WallpaperLibrary::addCandidate(int x, int y, int width, int height, const QString &name)
{
    if (m_candidatePath.isEmpty())
        return words("No picture was chosen.");
    const QImage image = readChecked(m_candidatePath, 0);
    if (image.isNull())
        return words("The picture could not be read.");
    const QSize canvas(ScreenWidth, ScreenHeight);
    const QRect part = crop::fitted(QRect(x, y, width, height), image.size(), canvas);
    if (part.isEmpty())
        return words("There is nothing to cut.");
    const QImage result = image.copy(part).scaled(canvas * SavedFactor, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    const QString folder = QDir(m_places.userRoot).filePath(QString::fromLatin1(MineStyle));
    if (!QDir().mkpath(folder))
        return words("The folder of your pictures cannot be created.");
    const QString base = filenames::safeName(name.trimmed().isEmpty() ? QFileInfo(m_candidatePath).completeBaseName() : name, QStringLiteral("wallpaper"));
    const QString target = filenames::freePath(folder, base, QStringLiteral(".jpg"));
    QSaveFile file(target); // written whole, or not at all
    if (!file.open(QIODevice::WriteOnly) || !result.save(&file, "JPEG", JpegQuality) || !file.commit())
        return words("The picture could not be saved.");
    clearCandidate();
    reload();
    return {};
}

WallpaperThumbnails::WallpaperThumbnails(const WallpaperLibrary *library)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_library(library)
{
}

QImage WallpaperThumbnails::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    Q_UNUSED(requestedSize)
    const QImage image = m_library->thumbnail(id);
    if (size)
        *size = image.size();
    return image;
}

WallpaperPreview::WallpaperPreview(const WallpaperLibrary *library)
    : QQuickImageProvider(QQuickImageProvider::Image)
    , m_library(library)
{
}

QImage WallpaperPreview::requestImage(const QString &id, QSize *size, const QSize &requestedSize)
{
    Q_UNUSED(id)
    Q_UNUSED(requestedSize)
    const QImage image = m_library->candidatePreview();
    if (size)
        *size = image.size();
    return image;
}

} // namespace bosun
