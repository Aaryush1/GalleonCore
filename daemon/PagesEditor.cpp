// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <InitialPages.h>

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
    QByteArray bytes = initialPagesJson();
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

// The JSON action of a key from what the window says.
QJsonObject actionOf(const KeySpec &spec)
{
    QJsonObject action{{QStringLiteral("type"), spec.type}};
    if (spec.type == QLatin1String("app")) {
        action.insert(QStringLiteral("desktop"), spec.desktop);
    } else if (spec.type == QLatin1String("media")) {
        action.insert(QStringLiteral("command"), spec.command);
        if (spec.command == QLatin1String("volume"))
            action.insert(QStringLiteral("step"), spec.step);
        else if (spec.command == QLatin1String("seek"))
            action.insert(QStringLiteral("seconds"), spec.seconds);
    } else if (spec.type == QLatin1String("page")) {
        action.insert(QStringLiteral("page"), spec.page);
    } else if (spec.type == QLatin1String("key")) {
        action.insert(QStringLiteral("key"), spec.keyName);
    } else if (spec.type == QLatin1String("steam")) {
        action.insert(QStringLiteral("appid"), spec.appid);
    }
    return action;
}

// The icon a key writes: nothing when there is none.
void putIcon(QJsonObject &entry, const QString &icon)
{
    if (!icon.trimmed().isEmpty())
        entry.insert(QStringLiteral("icon"), icon.trimmed());
}

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

namespace {

// Pages (other than `except`) that a key of any page opens.
bool pageIsLinkedTo(const QJsonObject &root, const QString &page, const QString &except)
{
    const QJsonObject pages = root.value(QStringLiteral("pages")).toObject();
    for (auto it = pages.begin(); it != pages.end(); ++it) {
        if (it.key() == except)
            continue;
        const QJsonArray keys = it.value().toObject().value(QStringLiteral("keys")).toArray();
        for (const auto &key : keys) {
            const QJsonObject action = key.toObject().value(QStringLiteral("action")).toObject();
            if (action.value(QStringLiteral("type")).toString() == QLatin1String("page") && action.value(QStringLiteral("page")).toString() == page)
                return true;
        }
    }
    return false;
}

// Removes `page`, then the pages that only it led to (its "plus" pages), and so on: a page is kept
// when a theme has it, it is the home page, or a key of a page that stays still leads to it.
void removePageAndItsOrphans(QJsonObject &root, const QString &page)
{
    QJsonObject pages = root.value(QStringLiteral("pages")).toObject();
    QStringList led;
    for (const auto &key : pages.value(page).toObject().value(QStringLiteral("keys")).toArray()) {
        const QJsonObject action = key.toObject().value(QStringLiteral("action")).toObject();
        if (action.value(QStringLiteral("type")).toString() == QLatin1String("page"))
            led << action.value(QStringLiteral("page")).toString();
    }
    pages.remove(page);
    root.insert(QStringLiteral("pages"), pages);
    const QJsonArray themes = root.value(QStringLiteral("themes")).toArray();
    for (const QString &next : std::as_const(led)) {
        bool isThemePage = false;
        for (const auto &theme : themes)
            isThemePage = isThemePage || theme.toObject().value(QStringLiteral("page")).toString() == next;
        if (!pages.contains(next) || isThemePage || next == root.value(QStringLiteral("home")).toString())
            continue;
        if (!pageIsLinkedTo(root, next, next))
            removePageAndItsOrphans(root, next);
    }
}

bool validThemeIndex(const QJsonArray &themes, int index, EditResult &error)
{
    if (index >= 0 && index < themes.size())
        return true;
    error = failure(QStringLiteral("there is no theme number %1").arg(index));
    return false;
}

} // namespace

EditResult PagesEditor::addSubPage(const QString &file, const QString &parent, const QString &name, const QString &moreLabel,
                                   const QString &backLabel)
{
    QJsonObject root;
    bool original = false;
    EditResult error;
    if (!load(file, root, original, error))
        return error;
    QJsonObject pages = root.value(QStringLiteral("pages")).toObject();
    if (!pages.contains(parent))
        return failure(QStringLiteral("no page named '%1'").arg(parent));
    if (pages.contains(name))
        return failure(QStringLiteral("there is already a page named '%1'").arg(name));
    QJsonArray keys = pages.value(parent).toObject().value(QStringLiteral("keys")).toArray();
    // The last free key: the one that a "More" key usually takes.
    QList<bool> used(galleon::protocol::KeyCount, false);
    for (const auto &entry : std::as_const(keys)) {
        const int index = entry.toObject().value(QStringLiteral("key")).toInt(-1);
        if (index >= 0 && index < galleon::protocol::KeyCount)
            used[index] = true;
    }
    int free = -1;
    for (int index = galleon::protocol::KeyCount - 1; index >= 0 && free < 0; --index)
        free = used.at(index) ? -1 : index;
    if (free < 0)
        return failure(QStringLiteral("every key of this page is used: empty one first"));
    keys.append(QJsonObject{{QStringLiteral("key"), free},
                            {QStringLiteral("label"), moreLabel},
                            {QStringLiteral("action"), QJsonObject{{QStringLiteral("type"), QStringLiteral("page")}, {QStringLiteral("page"), name}}}});
    pages.insert(name, QJsonObject{{QStringLiteral("keys"),
                                    QJsonArray{QJsonObject{{QStringLiteral("key"), galleon::protocol::KeyCount - 1},
                                                           {QStringLiteral("label"), backLabel},
                                                           {QStringLiteral("action"), QJsonObject{{QStringLiteral("type"), QStringLiteral("back")}}}}}}});
    root.insert(QStringLiteral("pages"), pages);
    storeKeys(root, parent, keys);
    return write(file, root, original);
}

EditResult PagesEditor::addTheme(const QString &file, const QString &name, const QString &color, const QString &page)
{
    QJsonObject root;
    bool original = false;
    EditResult error;
    if (!load(file, root, original, error))
        return error;
    QJsonObject pages = root.value(QStringLiteral("pages")).toObject();
    if (pages.contains(page))
        return failure(QStringLiteral("there is already a page named '%1'").arg(page));
    pages.insert(page, QJsonObject{{QStringLiteral("keys"), QJsonArray()}});
    root.insert(QStringLiteral("pages"), pages);
    QJsonArray themes = root.value(QStringLiteral("themes")).toArray();
    themes.append(QJsonObject{{QStringLiteral("name"), name.trimmed()}, {QStringLiteral("page"), page}, {QStringLiteral("color"), color.trimmed().toLower()}});
    root.insert(QStringLiteral("themes"), themes);
    return write(file, root, original);
}

EditResult PagesEditor::setTheme(const QString &file, int index, const QString &name, const QString &color)
{
    QJsonObject root;
    bool original = false;
    EditResult error;
    if (!load(file, root, original, error))
        return error;
    QJsonArray themes = root.value(QStringLiteral("themes")).toArray();
    if (!validThemeIndex(themes, index, error))
        return error;
    QJsonObject theme = themes.at(index).toObject();
    theme.insert(QStringLiteral("name"), name.trimmed());
    theme.insert(QStringLiteral("color"), color.trimmed().toLower());
    themes.replace(index, theme);
    root.insert(QStringLiteral("themes"), themes);
    return write(file, root, original);
}

EditResult PagesEditor::removeTheme(const QString &file, int index)
{
    QJsonObject root;
    bool original = false;
    EditResult error;
    if (!load(file, root, original, error))
        return error;
    QJsonArray themes = root.value(QStringLiteral("themes")).toArray();
    if (!validThemeIndex(themes, index, error))
        return error;
    if (themes.size() <= 1)
        return failure(QStringLiteral("the last theme cannot be removed"));
    const QString page = themes.at(index).toObject().value(QStringLiteral("page")).toString();
    if (page == root.value(QStringLiteral("home")).toString())
        return failure(QStringLiteral("this theme opens the home page, where the module starts: it cannot be removed"));
    themes.removeAt(index);
    root.insert(QStringLiteral("themes"), themes);
    // The page goes with its theme, unless another theme or a key still leads to it.
    bool otherTheme = false;
    for (const auto &theme : std::as_const(themes))
        otherTheme = otherTheme || theme.toObject().value(QStringLiteral("page")).toString() == page;
    if (!otherTheme && !pageIsLinkedTo(root, page, page))
        removePageAndItsOrphans(root, page);
    return write(file, root, original);
}

EditResult PagesEditor::moveTheme(const QString &file, int index, int delta)
{
    QJsonObject root;
    bool original = false;
    EditResult error;
    if (!load(file, root, original, error))
        return error;
    QJsonArray themes = root.value(QStringLiteral("themes")).toArray();
    if (!validThemeIndex(themes, index, error))
        return error;
    const int target = index + delta;
    if (target < 0 || target >= themes.size())
        return failure(QStringLiteral("there is no place to move it to"));
    const QJsonValue moving = themes.at(index);
    themes.removeAt(index);
    themes.insert(target, moving);
    root.insert(QStringLiteral("themes"), themes);
    return write(file, root, original);
}

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

EditResult PagesEditor::setKey(const QString &file, const QString &page, int key, const KeySpec &spec)
{
    static const QStringList types{QStringLiteral("app"), QStringLiteral("media"), QStringLiteral("page"), QStringLiteral("back"),
                                   QStringLiteral("key"), QStringLiteral("steam")};
    if (!types.contains(spec.type))
        return failure(QStringLiteral("a key does one of: %1").arg(types.join(QStringLiteral(", "))));
    if (spec.type == QLatin1String("app")) {
        if (!DesktopEntries::isValidReference(spec.desktop))
            return failure(QStringLiteral("'%1' is not a desktop file name (such as firefox.desktop)").arg(spec.desktop.left(80)));
        if (!DesktopEntries::lookup(spec.desktop).isValid())
            return failure(QStringLiteral("no application named '%1'").arg(spec.desktop));
    }
    QJsonObject root;
    bool original = false;
    EditResult error;
    if (!load(file, root, original, error))
        return error;
    QJsonArray keys;
    // Remember the words of the old key before it goes.
    QJsonValue words;
    const QJsonArray before = root.value(QStringLiteral("pages")).toObject().value(page).toObject().value(QStringLiteral("keys")).toArray();
    for (const auto &entry : before) {
        if (entry.toObject().value(QStringLiteral("key")).toInt(-1) == key)
            words = entry.toObject().value(QStringLiteral("state_labels"));
    }
    if (!pageKeys(root, page, key, keys, error))
        return error;
    QJsonObject entry{{QStringLiteral("key"), key}, {QStringLiteral("action"), actionOf(spec)}};
    if (!spec.label.trimmed().isEmpty())
        entry.insert(QStringLiteral("label"), spec.label.trimmed());
    putIcon(entry, spec.icon);
    if (!words.isUndefined() && spec.type == QLatin1String("media")
        && (spec.command == QLatin1String("shuffle") || spec.command == QLatin1String("repeat")))
        entry.insert(QStringLiteral("state_labels"), words);
    keys.append(entry);
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
