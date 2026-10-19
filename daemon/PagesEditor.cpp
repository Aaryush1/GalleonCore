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
    return {false, {message}};
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
        error = {false, parsed.errors};
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
    std::sort(sorted.begin(), sorted.end(), [](const QJsonValue &a, const QJsonValue &b) {
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

EditResult write(const QString &file, const QJsonObject &root, bool original)
{
    const QByteArray bytes = QJsonDocument(root).toJson(QJsonDocument::Indented);
    // What is written must be a valid pages file, like any other.
    const PagesParseResult check = parsePagesConfig(bytes);
    if (!check.config) {
        EditResult result{false, check.errors};
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
    return {true, {}};
}

} // namespace

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
