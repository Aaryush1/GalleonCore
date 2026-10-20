// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PagesEditor.h"

#include "DesktopEntries.h"
#include "PagesConfig.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <algorithm>

namespace galleond {

namespace {

EditResult failure(const QString &message)
{
    return {.ok=false, .errors={message}};
}

// Reads the file (or the built-in pages when there is none), checks it, and
// hands back its JSON. `original` tells whether a file existed.
bool load(const QString &file, QJsonObject &root, bool &original, EditResult &error)
{
    QByteArray bytes = defaultPagesJson();
    original = QFileInfo::exists(file);
    if (original) {
        QFile in(file);
        if (!in.open(QIODevice::ReadOnly)) {
            error = failure(QStringLiteral("cannot read %1: %2").arg(file, in.errorString()));
            return false;
        }
        bytes = in.read(MaxPagesFileBytes + 1);
    }
    const PagesParseResult parsed = parsePagesConfig(bytes);
    if (!parsed.config) {
        // Never rewrite a file we do not understand: the user fixes it first.
        error = {.ok=false, .errors=parsed.errors};
        error.errors.prepend(QStringLiteral("the pages file has errors and was not changed:"));
        return false;
    }
    root = QJsonDocument::fromJson(bytes).object();
    return true;
}

bool pageKeys(QJsonObject &root, const QString &page, int key, QJsonArray &keys, EditResult &error)
{
    if (key < 0 || key >= galleon::protocol::KeyCount) {
        error = failure(QStringLiteral("key must be from 0 to %1").arg(galleon::protocol::KeyCount - 1));
        return false;
    }
    const QJsonObject pages = root.value(QStringLiteral("pages")).toObject();
    if (!pages.contains(page)) {
        error = failure(QStringLiteral("no page named '%1'").arg(page));
        return false;
    }
    keys = pages.value(page).toObject().value(QStringLiteral("keys")).toArray();
    // The key's old entry goes: whatever label or icon it had was for another action.
    for (qsizetype i = keys.size() - 1; i >= 0; --i) {
        if (keys.at(i).toObject().value(QStringLiteral("key")).toInt(-1) == key)
            keys.removeAt(i);
    }
    return true;
}

void storeKeys(QJsonObject &root, const QString &page, QJsonArray keys)
{
    QList<QJsonValue> sorted(keys.begin(), keys.end());
    std::ranges::sort(sorted, [](const QJsonValue &a, const QJsonValue &b) {
        return a.toObject().value(QStringLiteral("key")).toInt() < b.toObject().value(QStringLiteral("key")).toInt();
    });
    QJsonArray ordered;
    for (const QJsonValue &value : std::as_const(sorted))
        ordered.append(value);
    QJsonObject pages = root.value(QStringLiteral("pages")).toObject();
    QJsonObject pageObject = pages.value(page).toObject();
    pageObject.insert(QStringLiteral("keys"), ordered);
    pages.insert(page, pageObject);
    root.insert(QStringLiteral("pages"), pages);
}

EditResult write(const QString &file, const QJsonObject &root, bool original);

// The videos of a "screensaver" object as a list of {file, title}, whichever way the
// file wrote them: one path, one object, or a list of either.
QJsonArray videoEntries(const QJsonObject &saver)
{
    const QJsonValue value = saver.value(QStringLiteral("video"));
    QJsonArray listed;
    if (value.isArray())
        listed = value.toArray();
    else if (!value.isUndefined())
        listed.append(value);
    QJsonArray entries;
    for (const auto &entry : std::as_const(listed)) {
        const QJsonValue shape = entry;
        entries.append(shape.isObject() ? shape.toObject() : QJsonObject{{QStringLiteral("file"), shape.toString()}});
    }
    return entries;
}

// Puts the videos back: nothing when there are none (the key is not written as an empty
// list, which the file format refuses), a plain path when a video has no title.
void storeVideos(QJsonObject &saver, const QJsonArray &entries)
{
    if (entries.isEmpty()) {
        saver.remove(QStringLiteral("video"));
        return;
    }
    QJsonArray written;
    for (const auto &entry : entries) {
        const QJsonObject item = entry.toObject();
        written.append(item.contains(QStringLiteral("title")) ? QJsonValue(item) : item.value(QStringLiteral("file")));
    }
    saver.insert(QStringLiteral("video"), written);
}

// The place a path in the pages file points at, to tell that two spellings are one file.
QString placeOf(const QString &path, const QString &pagesFile)
{
    QString text = path;
    if (text.startsWith(QLatin1String("~/")))
        text = QDir::homePath() + text.mid(1);
    return QDir::cleanPath(QDir::isRelativePath(text) ? QFileInfo(pagesFile).absolutePath() + QLatin1Char('/') + text : text);
}

bool validIndex(const QJsonArray &entries, int index, EditResult &error)
{
    if (index >= 0 && index < entries.size())
        return true;
    error = failure(QStringLiteral("there is no video number %1").arg(index));
    return false;
}

// Reads the file, lets `change` modify its "screensaver" object (made when there is none),
// and writes the result. `change` returns an error to refuse the edit.
template <typename Change>
EditResult editScreensaver(const QString &file, Change change)
{
    QJsonObject root;
    bool original = false;
    EditResult error;
    if (!load(file, root, original, error))
        return error;
    QJsonObject saver = root.value(QStringLiteral("screensaver")).toObject();
    if (const EditResult refused = change(saver); !refused.ok)
        return refused;
    root.insert(QStringLiteral("screensaver"), saver);
    return write(file, root, original);
}

EditResult write(const QString &file, const QJsonObject &root, bool original)
{
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    // What is written must be a valid pages file, like any other.
    const PagesParseResult check = parsePagesConfig(bytes);
    if (!check.config) {
        EditResult result{.ok=false, .errors=check.errors};
        result.errors.prepend(QStringLiteral("the edit would make the file invalid, nothing was written:"));
        return result;
    }

    QDir().mkpath(QFileInfo(file).absolutePath());
    QFile::Permissions permissions = QFile::ReadOwner | QFile::WriteOwner | QFile::ReadGroup | QFile::ReadOther;
    if (original) {
        permissions = QFileInfo(file).permissions();
        const QString backup = file + QStringLiteral(".bak");
        QFile::remove(backup);
        QFile::copy(file, backup); // one backup: the file as it was before this edit
    }
    QSaveFile out(file);
    if (!out.open(QIODevice::WriteOnly))
        return failure(QStringLiteral("cannot write %1: %2").arg(file, out.errorString()));
    out.write(bytes);
    out.setPermissions(permissions & ~QFile::WriteOther);
    if (!out.commit())
        return failure(QStringLiteral("cannot write %1: %2").arg(file, out.errorString()));
    return {.ok=true, .errors={}};
}

} // namespace

EditResult PagesEditor::setScreensaverIdleMinutes(const QString &file, int minutes)
{
    if (minutes < 1 || minutes > 720)
        return failure(QStringLiteral("the screensaver starts after 1 to 720 minutes"));
    return editScreensaver(file, [minutes](QJsonObject &saver) {
        saver.insert(QStringLiteral("idle_minutes"), minutes);
        return EditResult{.ok = true, .errors = {}};
    });
}

EditResult PagesEditor::setScreensaverFps(const QString &file, int fps)
{
    if (fps < 1 || fps > 20)
        return failure(QStringLiteral("the module shows 1 to 20 images per second"));
    return editScreensaver(file, [fps](QJsonObject &saver) {
        saver.insert(QStringLiteral("fps"), fps);
        return EditResult{.ok = true, .errors = {}};
    });
}

EditResult PagesEditor::setScreensaverWhilePlaying(const QString &file, bool whilePlaying)
{
    return editScreensaver(file, [whilePlaying](QJsonObject &saver) {
        saver.insert(QStringLiteral("while_playing"), whilePlaying);
        return EditResult{.ok = true, .errors = {}};
    });
}

EditResult PagesEditor::setScreensaverVideoTitle(const QString &file, int index, const QString &title)
{
    return editScreensaver(file, [index, title](QJsonObject &saver) {
        QJsonArray entries = videoEntries(saver);
        EditResult error;
        if (!validIndex(entries, index, error))
            return error;
        QJsonObject item = entries.at(index).toObject();
        if (title.trimmed().isEmpty())
            item.remove(QStringLiteral("title"));
        else
            item.insert(QStringLiteral("title"), title.trimmed());
        entries.replace(index, item);
        storeVideos(saver, entries);
        return EditResult{.ok = true, .errors = {}};
    });
}

EditResult PagesEditor::addScreensaverVideo(const QString &file, const QString &video, const QString &title)
{
    if (video.trimmed().isEmpty())
        return failure(QStringLiteral("no video file was given"));
    return editScreensaver(file, [&](QJsonObject &saver) {
        QJsonArray entries = videoEntries(saver);
        if (entries.size() >= MaxScreensaverVideos)
            return failure(QStringLiteral("the screensaver can show %1 videos at most: remove one first").arg(MaxScreensaverVideos));
        const QString place = placeOf(video, file);
        for (const auto &entry : std::as_const(entries)) {
            if (placeOf(entry.toObject().value(QStringLiteral("file")).toString(), file) == place)
                return failure(QStringLiteral("this video is already in the list"));
        }
        QJsonObject item{{QStringLiteral("file"), video}};
        if (!title.trimmed().isEmpty())
            item.insert(QStringLiteral("title"), title.trimmed());
        entries.append(item);
        storeVideos(saver, entries);
        return EditResult{.ok = true, .errors = {}};
    });
}

EditResult PagesEditor::removeScreensaverVideo(const QString &file, int index)
{
    return editScreensaver(file, [index](QJsonObject &saver) {
        QJsonArray entries = videoEntries(saver);
        EditResult error;
        if (!validIndex(entries, index, error))
            return error;
        entries.removeAt(index);
        storeVideos(saver, entries);
        return EditResult{.ok = true, .errors = {}};
    });
}

EditResult PagesEditor::assignApp(const QString &file, const QString &page, int key, const QString &desktop)
{
    if (!DesktopEntries::isValidReference(desktop))
        return failure(QStringLiteral("'%1' is not a desktop file name (such as firefox.desktop)").arg(desktop.left(80)));
    // A typo must not be saved: the application has to exist now.
    if (!DesktopEntries::lookup(desktop).isValid())
        return failure(QStringLiteral("no application named '%1'").arg(desktop));

    QJsonObject root;
    bool original = false;
    EditResult error;
    if (!load(file, root, original, error))
        return error;
    QJsonArray keys;
    if (!pageKeys(root, page, key, keys, error))
        return error;

    keys.append(QJsonObject{{QStringLiteral("key"), key},
                            {QStringLiteral("action"),
                             QJsonObject{{QStringLiteral("type"), QStringLiteral("app")}, {QStringLiteral("desktop"), desktop}}}});
    storeKeys(root, page, keys);
    return write(file, root, original);
}

EditResult PagesEditor::clearKey(const QString &file, const QString &page, int key)
{
    QJsonObject root;
    bool original = false;
    EditResult error;
    if (!load(file, root, original, error))
        return error;
    QJsonArray keys;
    if (!pageKeys(root, page, key, keys, error))
        return error;
    storeKeys(root, page, keys);
    return write(file, root, original);
}

} // namespace galleond
