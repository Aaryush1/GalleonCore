// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PagesConfig.h"

#include "DesktopEntries.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <cmath>

namespace galleond {

namespace {

const QRegularExpression PageNamePattern(QStringLiteral("^[a-z0-9_-]{1,32}$"));
// An icon is a theme icon name or a file name next to the pages file: no slash,
// so no path can leave that folder.
const QRegularExpression IconNamePattern(QStringLiteral("^[A-Za-z0-9._-]{1,64}$"));

const QRegularExpression ThemeColorPattern(QStringLiteral("^#[0-9A-Fa-f]{6}$"));

struct Parser
{
    QStringList errors;
    QSet<QString> referencedPages; // checked once all pages are known

    void error(const QString &path, const QString &message) { errors.append(path + QStringLiteral(": ") + message); }

    // Every field must be one of `allowed`: a misspelt one is an error, not silence.
    void rejectUnknown(const QJsonObject &object, const QStringList &allowed, const QString &path)
    {
        for (auto it = object.begin(); it != object.end(); ++it) {
            if (!allowed.contains(it.key()))
                error(path, QStringLiteral("unknown field '%1'").arg(it.key().left(MaxNameLength)));
        }
    }

    // A whole number in [min, max]; false (and an error) otherwise.
    bool integer(const QJsonValue &value, int min, int max, const QString &path, int &out)
    {
        if (!value.isDouble()) {
            error(path, QStringLiteral("must be a number"));
            return false;
        }
        const double number = value.toDouble();
        if (std::floor(number) != number || number < min || number > max) {
            error(path, QStringLiteral("must be a whole number from %1 to %2").arg(min).arg(max));
            return false;
        }
        out = static_cast<int>(number);
        return true;
    }

    bool text(const QJsonValue &value, const QString &path, int maxLength, QString &out)
    {
        if (!value.isString()) {
            error(path, QStringLiteral("must be a string"));
            return false;
        }
        out = value.toString();
        if (out.size() > maxLength) {
            error(path, QStringLiteral("is longer than %1 characters").arg(maxLength));
            return false;
        }
        return true;
    }

    bool iconName(const QJsonValue &value, const QString &path, QString &out)
    {
        QString name;
        if (!text(value, path, MaxNameLength, name))
            return false;
        if (!IconNamePattern.match(name).hasMatch()) {
            error(path, QStringLiteral("must be an icon name or a file name (letters, digits, . _ -), no path"));
            return false;
        }
        out = name;
        return true;
    }

    void icon(const QJsonValue &value, const QString &path, IconSpec &out)
    {
        if (value.isString()) {
            iconName(value, path, out.name);
            return;
        }
        if (!value.isObject()) {
            error(path, QStringLiteral("must be a name or an object of states"));
            return;
        }
        const QJsonObject states = value.toObject();
        rejectUnknown(states, {QStringLiteral("playing"), QStringLiteral("paused"), QStringLiteral("stopped")}, path);
        for (const QString &state : {QStringLiteral("playing"), QStringLiteral("paused"), QStringLiteral("stopped")}) {
            if (!states.contains(state))
                continue;
            QString name;
            if (iconName(states.value(state), path + QLatin1Char('.') + state, name))
                out.byState.insert(state, name);
        }
        if (states.isEmpty())
            error(path, QStringLiteral("has no state"));
    }

    void action(const QJsonValue &value, const QString &path, Action &out, bool turnAction = false)
    {
        if (!value.isObject()) {
            error(path, QStringLiteral("must be an object"));
            return;
        }
        const QJsonObject object = value.toObject();
        const QString type = object.value(QStringLiteral("type")).toString();

        if (type == QLatin1String("none")) {
            rejectUnknown(object, {QStringLiteral("type")}, path);
            if (turnAction)
                error(path, QStringLiteral("a dial turn needs a media volume or seek action"));
            return;
        }
        if (type == QLatin1String("back")) {
            rejectUnknown(object, {QStringLiteral("type")}, path);
            if (turnAction)
                error(path, QStringLiteral("a dial turn needs a media volume or seek action"));
            out.type = ActionType::Back;
            return;
        }
        if (type == QLatin1String("wallpaper-mode")) {
            rejectUnknown(object, {QStringLiteral("type")}, path);
            if (turnAction)
                error(path, QStringLiteral("a dial turn needs a media volume or seek action"));
            out.type = ActionType::WallpaperMode;
            return;
        }
        if (type == QLatin1String("theme")) {
            rejectUnknown(object, {QStringLiteral("type")}, path);
            if (turnAction)
                error(path, QStringLiteral("a dial turn needs a media volume or seek action"));
            out.type = ActionType::Theme;
            return;
        }
        if (type == QLatin1String("app")) {
            rejectUnknown(object, {QStringLiteral("type"), QStringLiteral("desktop")}, path);
            if (turnAction)
                error(path, QStringLiteral("a dial turn needs a media volume or seek action"));
            QString desktop;
            if (text(object.value(QStringLiteral("desktop")), path + QStringLiteral(".desktop"), 200, desktop)) {
                if (!DesktopEntries::isValidReference(desktop)) {
                    error(path + QStringLiteral(".desktop"),
                          QStringLiteral("must be a desktop file name such as firefox.desktop, or the absolute path of a .desktop file"));
                } else {
                    out.type = ActionType::App;
                    out.desktop = desktop;
                }
            }
            return;
        }
        if (type == QLatin1String("page")) {
            rejectUnknown(object, {QStringLiteral("type"), QStringLiteral("page")}, path);
            if (turnAction)
                error(path, QStringLiteral("a dial turn needs a media volume or seek action"));
            QString page;
            if (text(object.value(QStringLiteral("page")), path + QStringLiteral(".page"), MaxNameLength, page)) {
                out.type = ActionType::Page;
                out.page = page;
                referencedPages.insert(page);
            }
            return;
        }
        if (type != QLatin1String("media")) {
            error(path + QStringLiteral(".type"), QStringLiteral("must be one of none, media, page, back, app, theme, wallpaper-mode"));
            return;
        }

        const QString command = object.value(QStringLiteral("command")).toString();
        out.type = ActionType::Media;
        if (command == QLatin1String("play-pause") || command == QLatin1String("next") || command == QLatin1String("previous")
            || command == QLatin1String("stop")) {
            rejectUnknown(object, {QStringLiteral("type"), QStringLiteral("command")}, path);
            if (turnAction)
                error(path, QStringLiteral("a dial turn needs a media volume or seek action"));
            out.command = command == QLatin1String("next")       ? MediaCommand::Next
                          : command == QLatin1String("previous") ? MediaCommand::Previous
                          : command == QLatin1String("stop")     ? MediaCommand::Stop
                                                                  : MediaCommand::PlayPause;
        } else if (command == QLatin1String("volume")) {
            rejectUnknown(object, {QStringLiteral("type"), QStringLiteral("command"), QStringLiteral("step")}, path);
            out.command = MediaCommand::Volume;
            if (integer(object.value(QStringLiteral("step")), -100, 100, path + QStringLiteral(".step"), out.step) && out.step == 0)
                error(path + QStringLiteral(".step"), QStringLiteral("must not be 0"));
        } else if (command == QLatin1String("seek")) {
            rejectUnknown(object, {QStringLiteral("type"), QStringLiteral("command"), QStringLiteral("seconds")}, path);
            out.command = MediaCommand::Seek;
            if (integer(object.value(QStringLiteral("seconds")), -3600, 3600, path + QStringLiteral(".seconds"), out.seconds)
                && out.seconds == 0)
                error(path + QStringLiteral(".seconds"), QStringLiteral("must not be 0"));
        } else {
            error(path + QStringLiteral(".command"),
                  QStringLiteral("must be one of play-pause, next, previous, stop, volume, seek"));
        }
    }

    void key(const QJsonValue &value, const QString &path, Page &page)
    {
        if (!value.isObject()) {
            error(path, QStringLiteral("must be an object"));
            return;
        }
        const QJsonObject object = value.toObject();
        rejectUnknown(object,
                      {QStringLiteral("key"), QStringLiteral("label"), QStringLiteral("icon"), QStringLiteral("action")},
                      path);

        KeyConfig config;
        if (!integer(object.value(QStringLiteral("key")), 0, galleon::protocol::KeyCount - 1, path + QStringLiteral(".key"),
                     config.key))
            return;
        if (page.keys[static_cast<std::size_t>(config.key)]) {
            error(path + QStringLiteral(".key"), QStringLiteral("key %1 is already used on this page").arg(config.key));
            return;
        }
        if (object.contains(QStringLiteral("label")))
            text(object.value(QStringLiteral("label")), path + QStringLiteral(".label"), MaxLabelLength, config.label);
        if (object.contains(QStringLiteral("icon")))
            icon(object.value(QStringLiteral("icon")), path + QStringLiteral(".icon"), config.icon);
        if (object.contains(QStringLiteral("action")))
            action(object.value(QStringLiteral("action")), path + QStringLiteral(".action"), config.action);
        page.keys[static_cast<std::size_t>(config.key)] = config;
    }

    void pages(const QJsonValue &value, PagesConfig &config)
    {
        if (!value.isObject() || value.toObject().isEmpty()) {
            error(QStringLiteral("pages"), QStringLiteral("must be an object with at least one page"));
            return;
        }
        const QJsonObject all = value.toObject();
        if (all.size() > MaxPages)
            error(QStringLiteral("pages"), QStringLiteral("has more than %1 pages").arg(MaxPages));

        for (auto it = all.begin(); it != all.end(); ++it) {
            const QString path = QStringLiteral("pages.") + it.key().left(MaxNameLength);
            if (!PageNamePattern.match(it.key()).hasMatch()) {
                error(path, QStringLiteral("a page name is 1 to 32 characters of a-z, 0-9, _ or -"));
                continue;
            }
            if (!it.value().isObject()) {
                error(path, QStringLiteral("must be an object"));
                continue;
            }
            const QJsonObject pageObject = it.value().toObject();
            rejectUnknown(pageObject, {QStringLiteral("keys")}, path);
            Page page;
            const QJsonValue keys = pageObject.value(QStringLiteral("keys"));
            if (!keys.isArray()) {
                error(path + QStringLiteral(".keys"), QStringLiteral("must be an array"));
            } else {
                const QJsonArray array = keys.toArray();
                if (array.size() > galleon::protocol::KeyCount)
                    error(path + QStringLiteral(".keys"),
                          QStringLiteral("has more than %1 keys").arg(galleon::protocol::KeyCount));
                for (qsizetype i = 0; i < array.size() && i < galleon::protocol::KeyCount; ++i)
                    key(array.at(i), QStringLiteral("%1.keys[%2]").arg(path).arg(i), page);
            }
            config.pages.insert(it.key(), page);
        }
    }

    void themes(const QJsonValue &value, PagesConfig &config)
    {
        if (value.isUndefined())
            return;
        if (!value.isArray()) {
            error(QStringLiteral("themes"), QStringLiteral("must be an array"));
            return;
        }
        const QJsonArray array = value.toArray();
        if (array.size() > MaxThemes)
            error(QStringLiteral("themes"), QStringLiteral("has more than %1 themes").arg(MaxThemes));
        for (qsizetype i = 0; i < array.size() && i < MaxThemes; ++i) {
            const QString path = QStringLiteral("themes[%1]").arg(i);
            if (!array.at(i).isObject()) {
                error(path, QStringLiteral("must be an object"));
                continue;
            }
            const QJsonObject object = array.at(i).toObject();
            rejectUnknown(object, {QStringLiteral("name"), QStringLiteral("page"), QStringLiteral("color")}, path);
            Theme theme;
            bool ok = text(object.value(QStringLiteral("name")), path + QStringLiteral(".name"), MaxLabelLength, theme.name);
            if (ok && theme.name.isEmpty()) {
                error(path + QStringLiteral(".name"), QStringLiteral("must not be empty"));
                ok = false;
            }
            ok = text(object.value(QStringLiteral("page")), path + QStringLiteral(".page"), MaxNameLength, theme.page) && ok;
            QString color;
            if (!text(object.value(QStringLiteral("color")), path + QStringLiteral(".color"), 7, color)) {
                ok = false;
            } else if (!ThemeColorPattern.match(color).hasMatch()) {
                error(path + QStringLiteral(".color"), QStringLiteral("must be a colour such as #00bcd4"));
                ok = false;
            } else {
                theme.rgb = color.mid(1).toUInt(nullptr, 16);
            }
            if (!ok)
                continue;
            referencedPages.insert(theme.page);
            config.themes.push_back(theme);
        }
    }

    // [a, b] with both numbers in [min, max].
    bool pair(const QJsonValue &value, const QString &path, double min, double max, double &first, double &second)
    {
        if (!value.isArray() || value.toArray().size() != 2 || !value.toArray().at(0).isDouble()
            || !value.toArray().at(1).isDouble()) {
            error(path, QStringLiteral("must be two numbers [width, height] in millimetres"));
            return false;
        }
        first = value.toArray().at(0).toDouble();
        second = value.toArray().at(1).toDouble();
        if (!(first >= min && first <= max && second >= min && second <= max)) {
            error(path, QStringLiteral("numbers must be from %1 to %2 mm").arg(min).arg(max));
            return false;
        }
        return true;
    }

    void screensaver(const QJsonValue &value, PagesConfig &config)
    {
        if (value.isUndefined())
            return;
        if (!value.isObject()) {
            error(QStringLiteral("screensaver"), QStringLiteral("must be an object"));
            return;
        }
        const QJsonObject object = value.toObject();
        const QString path = QStringLiteral("screensaver");
        rejectUnknown(object,
                      {QStringLiteral("video"), QStringLiteral("idle_minutes"), QStringLiteral("fps"),
                       QStringLiteral("while_playing"), QStringLiteral("geometry")},
                      path);
        ScreensaverConfig &target = config.screensaver;
        if (object.contains(QStringLiteral("video"))) {
            QString video;
            if (text(object.value(QStringLiteral("video")), path + QStringLiteral(".video"), MaxVideoPathLength, video)) {
                if (video.contains(QLatin1Char('\0')) || video.contains(QLatin1Char('\n')))
                    error(path + QStringLiteral(".video"), QStringLiteral("is not a file path"));
                else
                    target.video = video;
            }
        }
        if (object.contains(QStringLiteral("idle_minutes")))
            integer(object.value(QStringLiteral("idle_minutes")), 1, 720, path + QStringLiteral(".idle_minutes"), target.idleMinutes);
        if (object.contains(QStringLiteral("fps")))
            integer(object.value(QStringLiteral("fps")), 1, 20, path + QStringLiteral(".fps"), target.fps);
        if (object.contains(QStringLiteral("while_playing"))) {
            if (!object.value(QStringLiteral("while_playing")).isBool())
                error(path + QStringLiteral(".while_playing"), QStringLiteral("must be true or false"));
            else
                target.whilePlaying = object.value(QStringLiteral("while_playing")).toBool();
        }
        if (!object.contains(QStringLiteral("geometry")))
            return;
        const QString gpath = path + QStringLiteral(".geometry");
        if (!object.value(QStringLiteral("geometry")).isObject()) {
            error(gpath, QStringLiteral("must be an object"));
            return;
        }
        const QJsonObject geometry = object.value(QStringLiteral("geometry")).toObject();
        rejectUnknown(geometry,
                      {QStringLiteral("screen_mm"), QStringLiteral("key_mm"), QStringLiteral("gap_mm"),
                       QStringLiteral("screen_to_keys_mm")},
                      gpath);
        bool ok = pair(geometry.value(QStringLiteral("screen_mm")), gpath + QStringLiteral(".screen_mm"), 1, 500,
                       target.screenWidthMm, target.screenHeightMm);
        ok = pair(geometry.value(QStringLiteral("key_mm")), gpath + QStringLiteral(".key_mm"), 1, 100, target.keyWidthMm,
                  target.keyHeightMm) && ok;
        ok = pair(geometry.value(QStringLiteral("gap_mm")), gpath + QStringLiteral(".gap_mm"), 0, 100, target.gapXMm,
                  target.gapYMm) && ok;
        const QJsonValue toKeys = geometry.value(QStringLiteral("screen_to_keys_mm"));
        if (!toKeys.isDouble() || toKeys.toDouble() < 0 || toKeys.toDouble() > 200) {
            error(gpath + QStringLiteral(".screen_to_keys_mm"), QStringLiteral("must be a number from 0 to 200 mm"));
            ok = false;
        } else {
            target.screenToKeysMm = toKeys.toDouble();
        }
        target.hasGeometry = ok;
    }

    void screen(const QJsonValue &value, PagesConfig &config)
    {
        if (value.isUndefined())
            return;
        if (!value.isObject()) {
            error(QStringLiteral("screen"), QStringLiteral("must be an object"));
            return;
        }
        const QJsonObject object = value.toObject();
        rejectUnknown(object,
                      {QStringLiteral("now_playing"), QStringLiteral("remote_covers"), QStringLiteral("video_thumbnails")},
                      QStringLiteral("screen"));
        if (object.contains(QStringLiteral("now_playing"))) {
            const QJsonValue mode = object.value(QStringLiteral("now_playing"));
            const QString text = mode.toString();
            if (!mode.isString() || (text != QLatin1String("playing") && text != QLatin1String("playing-or-paused")
                                     && text != QLatin1String("never")))
                error(QStringLiteral("screen.now_playing"), QStringLiteral("must be playing, playing-or-paused or never"));
            else
                config.screen.nowPlaying = text == QLatin1String("never")               ? NowPlayingMode::Never
                                           : text == QLatin1String("playing-or-paused") ? NowPlayingMode::PlayingOrPaused
                                                                                         : NowPlayingMode::Playing;
        }
        if (object.contains(QStringLiteral("video_thumbnails"))) {
            if (!object.value(QStringLiteral("video_thumbnails")).isBool())
                error(QStringLiteral("screen.video_thumbnails"), QStringLiteral("must be true or false"));
            else
                config.screen.videoThumbnails = object.value(QStringLiteral("video_thumbnails")).toBool();
        }
        if (object.contains(QStringLiteral("remote_covers"))) {
            if (!object.value(QStringLiteral("remote_covers")).isBool())
                error(QStringLiteral("screen.remote_covers"), QStringLiteral("must be true or false"));
            else
                config.screen.remoteCovers = object.value(QStringLiteral("remote_covers")).toBool();
        }
    }

    void dials(const QJsonValue &value, PagesConfig &config)
    {
        if (value.isUndefined())
            return;
        if (!value.isObject()) {
            error(QStringLiteral("dials"), QStringLiteral("must be an object"));
            return;
        }
        const QJsonObject object = value.toObject();
        rejectUnknown(object, {QStringLiteral("0"), QStringLiteral("1")}, QStringLiteral("dials"));
        for (int dial = 0; dial < galleon::protocol::DialCount; ++dial) {
            const QString name = QString::number(dial);
            if (!object.contains(name))
                continue;
            const QString path = QStringLiteral("dials.") + name;
            if (!object.value(name).isObject()) {
                error(path, QStringLiteral("must be an object"));
                continue;
            }
            const QJsonObject dialObject = object.value(name).toObject();
            rejectUnknown(dialObject, {QStringLiteral("turn"), QStringLiteral("press")}, path);
            DialConfig &target = config.dials[static_cast<std::size_t>(dial)];
            if (dialObject.contains(QStringLiteral("turn"))) {
                action(dialObject.value(QStringLiteral("turn")), path + QStringLiteral(".turn"), target.turn, true);
                if (target.turn.type == ActionType::Media && target.turn.command != MediaCommand::Volume
                    && target.turn.command != MediaCommand::Seek)
                    error(path + QStringLiteral(".turn"), QStringLiteral("a dial turn needs a media volume or seek action"));
            }
            if (dialObject.contains(QStringLiteral("press")))
                action(dialObject.value(QStringLiteral("press")), path + QStringLiteral(".press"), target.press);
        }
    }
};

} // namespace

PagesParseResult parsePagesConfig(const QByteArray &json)
{
    PagesParseResult result;
    if (json.size() > MaxPagesFileBytes) {
        result.errors.append(QStringLiteral("file: larger than %1 KiB").arg(MaxPagesFileBytes / 1024));
        return result;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        result.errors.append(QStringLiteral("file: not a JSON object (%1, at byte %2)")
                                 .arg(parseError.errorString())
                                 .arg(parseError.offset));
        return result;
    }
    const QJsonObject root = document.object();

    Parser parser;
    parser.rejectUnknown(root,
                         {QStringLiteral("version"), QStringLiteral("home"), QStringLiteral("pages"), QStringLiteral("dials"),
                          QStringLiteral("screen"), QStringLiteral("themes"), QStringLiteral("screensaver")},
                         QStringLiteral("file"));
    int version = 0;
    if (parser.integer(root.value(QStringLiteral("version")), 1, 1, QStringLiteral("version"), version) && version != 1)
        parser.error(QStringLiteral("version"), QStringLiteral("only version 1 is known"));

    PagesConfig config;
    parser.pages(root.value(QStringLiteral("pages")), config);
    parser.dials(root.value(QStringLiteral("dials")), config);
    parser.screen(root.value(QStringLiteral("screen")), config);
    parser.themes(root.value(QStringLiteral("themes")), config);
    parser.screensaver(root.value(QStringLiteral("screensaver")), config);

    parser.text(root.value(QStringLiteral("home")), QStringLiteral("home"), MaxNameLength, config.home);
    if (!config.home.isEmpty() && !config.pages.contains(config.home))
        parser.error(QStringLiteral("home"), QStringLiteral("page '%1' does not exist").arg(config.home));
    for (const QString &page : std::as_const(parser.referencedPages)) {
        if (!config.pages.contains(page))
            parser.error(QStringLiteral("action"), QStringLiteral("page '%1' does not exist").arg(page));
    }

    result.errors = parser.errors;
    if (result.errors.isEmpty())
        result.config = config;
    return result;
}

QByteArray defaultPagesJson()
{
    return QByteArrayLiteral(R"({
  "version": 1,
  "home": "main",
  "pages": {
    "main": {
      "keys": [
        { "key": 0, "label": "Previous", "icon": "media-skip-backward", "action": { "type": "media", "command": "previous" } },
        { "key": 1, "label": "Play",
          "icon": { "playing": "media-playback-pause", "paused": "media-playback-start", "stopped": "media-playback-start" },
          "action": { "type": "media", "command": "play-pause" } },
        { "key": 2, "label": "Next", "icon": "media-skip-forward", "action": { "type": "media", "command": "next" } },
        { "key": 3, "label": "Vol -", "icon": "audio-volume-low", "action": { "type": "media", "command": "volume", "step": -5 } },
        { "key": 5, "label": "Vol +", "icon": "audio-volume-high", "action": { "type": "media", "command": "volume", "step": 5 } },
        { "key": 6, "label": "-10 s", "icon": "media-seek-backward", "action": { "type": "media", "command": "seek", "seconds": -10 } },
        { "key": 8, "label": "+10 s", "icon": "media-seek-forward", "action": { "type": "media", "command": "seek", "seconds": 10 } },
        { "key": 11, "label": "Stop", "icon": "media-playback-stop", "action": { "type": "media", "command": "stop" } }
      ]
    }
  },
  "dials": {
    "0": { "turn": { "type": "media", "command": "volume", "step": 2 }, "press": { "type": "media", "command": "play-pause" } },
    "1": { "turn": { "type": "media", "command": "seek", "seconds": 5 }, "press": { "type": "media", "command": "next" } }
  }
})");
}

} // namespace galleond
