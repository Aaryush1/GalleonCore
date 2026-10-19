// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <PagesConfig.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTest>

using namespace galleond;

namespace {

QJsonObject defaultObject()
{
    return QJsonDocument::fromJson(defaultPagesJson()).object();
}

PagesParseResult parseObject(const QJsonObject &object)
{
    return parsePagesConfig(QJsonDocument(object).toJson());
}

// The default file with one change made by `edit`.
PagesParseResult parseEdited(const std::function<void(QJsonObject &)> &edit)
{
    QJsonObject object = defaultObject();
    edit(object);
    return parseObject(object);
}

QJsonObject keyObject(int key, const QString &icon = QStringLiteral("media-playback-stop"))
{
    return {{"key", key}, {"label", "X"}, {"icon", icon}, {"action", QJsonObject{{"type", "none"}}}};
}

// A page with one key, in a file whose home is that page.
QJsonObject fileWithKeys(const QJsonArray &keys)
{
    return {{"version", 1}, {"home", "p"}, {"pages", QJsonObject{{"p", QJsonObject{{"keys", keys}}}}}};
}

bool mentions(const PagesParseResult &result, const QString &text)
{
    for (const QString &line : result.errors) {
        if (line.contains(text))
            return true;
    }
    return false;
}

} // namespace

class TestPagesConfig : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void theDefaultFileIsValid()
    {
        const auto result = parsePagesConfig(defaultPagesJson());
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join('\n')));
        QVERIFY(result.config.has_value());
        QCOMPARE(result.config->home, QStringLiteral("main"));
        QCOMPARE(result.config->pages.size(), 1);
    }

    void theDefaultFileIsReadAsWritten()
    {
        const PagesConfig config = *parsePagesConfig(defaultPagesJson()).config;
        const Page &main = config.pages.value("main");

        QVERIFY(main.keys[0].has_value());
        QCOMPARE(main.keys[0]->label, QStringLiteral("Previous"));
        QCOMPARE(main.keys[0]->icon.name, QStringLiteral("media-skip-backward"));
        QCOMPARE(int(main.keys[0]->action.command), int(MediaCommand::Previous));

        QVERIFY(main.keys[1].has_value()); // the play key changes icon with the state
        QCOMPARE(main.keys[1]->icon.forState("playing"), QStringLiteral("media-playback-pause"));
        QCOMPARE(main.keys[1]->icon.forState("paused"), QStringLiteral("media-playback-start"));
        QCOMPARE(int(main.keys[1]->action.command), int(MediaCommand::PlayPause));

        QVERIFY(!main.keys[4].has_value()); // unused keys stay empty
        QCOMPARE(main.keys[3]->action.step, -5);
        QCOMPARE(main.keys[6]->action.seconds, -10);
        QVERIFY(main.keys[11].has_value());

        QCOMPARE(int(config.dials[0].turn.command), int(MediaCommand::Volume));
        QCOMPARE(config.dials[0].turn.step, 2);
        QCOMPARE(int(config.dials[0].press.command), int(MediaCommand::PlayPause));
        QCOMPARE(int(config.dials[1].turn.command), int(MediaCommand::Seek));
        QCOMPARE(config.dials[1].turn.seconds, 5);
    }

    void aSubMenuCanBeDeclared()
    {
        QJsonObject object = defaultObject();
        QJsonObject pages = object.value("pages").toObject();
        pages.insert("music", QJsonObject{{"keys", QJsonArray{QJsonObject{{"key", 11}, {"action", QJsonObject{{"type", "back"}}}}}}});
        QJsonObject main = pages.value("main").toObject();
        QJsonArray keys = main.value("keys").toArray();
        keys.append(QJsonObject{{"key", 9}, {"label", "More"}, {"action", QJsonObject{{"type", "page"}, {"page", "music"}}}});
        main.insert("keys", keys);
        pages.insert("main", main);
        object.insert("pages", pages);

        const auto result = parseObject(object);
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join('\n')));
        QCOMPARE(result.config->pages.size(), 2);
        QCOMPARE(int(result.config->pages.value("music").keys[11]->action.type), int(ActionType::Back));
    }

    void notJsonIsRefused()
    {
        QVERIFY(!parsePagesConfig("not json").config.has_value());
        QVERIFY(!parsePagesConfig("").config.has_value());
        QVERIFY(!parsePagesConfig("[1, 2]").config.has_value());
        QVERIFY(!parsePagesConfig("{\"version\": 1,}").config.has_value());
        QVERIFY(!parsePagesConfig("{").errors.isEmpty());
    }

    void anOversizedFileIsRefusedWithoutParsing()
    {
        QByteArray big = defaultPagesJson();
        big.append(QByteArray(MaxPagesFileBytes, ' '));
        const auto result = parsePagesConfig(big);
        QVERIFY(!result.config.has_value());
        QVERIFY(mentions(result, "larger than"));
    }

    void unknownFieldsAreErrors()
    {
        QVERIFY(mentions(parseEdited([](QJsonObject &o) { o.insert("colour", "red"); }), "unknown field 'colour'"));
        QVERIFY(mentions(parseEdited([](QJsonObject &o) {
                             QJsonObject pages = o.value("pages").toObject();
                             QJsonObject main = pages.value("main").toObject();
                             main.insert("title", "x");
                             pages.insert("main", main);
                             o.insert("pages", pages);
                         }),
                         "unknown field 'title'"));
    }

    void versionMustBeOne()
    {
        QVERIFY(!parseEdited([](QJsonObject &o) { o.insert("version", 2); }).config.has_value());
        QVERIFY(!parseEdited([](QJsonObject &o) { o.remove("version"); }).config.has_value());
        QVERIFY(!parseEdited([](QJsonObject &o) { o.insert("version", "1"); }).config.has_value());
    }

    void homeMustExist()
    {
        QVERIFY(mentions(parseEdited([](QJsonObject &o) { o.insert("home", "nowhere"); }), "does not exist"));
        QVERIFY(!parseEdited([](QJsonObject &o) { o.remove("home"); }).config.has_value());
    }

    void pageNamesAreChecked()
    {
        for (const QString &name : {QStringLiteral("Main"), QStringLiteral("a b"), QStringLiteral("../x"), QStringLiteral(""),
                                   QString(33, QLatin1Char('a'))}) {
            QJsonObject object = fileWithKeys({});
            object.insert("pages", QJsonObject{{name, QJsonObject{{"keys", QJsonArray{}}}}, {"p", QJsonObject{{"keys", QJsonArray{}}}}});
            QVERIFY2(!parseObject(object).config.has_value(), qPrintable(name));
        }
    }

    void tooManyPagesAreRefused()
    {
        QJsonObject pages;
        for (int i = 0; i <= MaxPages; ++i)
            pages.insert(QStringLiteral("p%1").arg(i), QJsonObject{{"keys", QJsonArray{}}});
        QVERIFY(!parseObject({{"version", 1}, {"home", "p0"}, {"pages", pages}}).config.has_value());
        pages.remove("p0");
        QVERIFY(parseObject({{"version", 1}, {"home", "p1"}, {"pages", pages}}).config.has_value());
    }

    void keyNumbersAreChecked()
    {
        QVERIFY(parseObject(fileWithKeys({keyObject(0), keyObject(11)})).config.has_value());
        QVERIFY(!parseObject(fileWithKeys({keyObject(12)})).config.has_value());
        QVERIFY(!parseObject(fileWithKeys({keyObject(-1)})).config.has_value());
        QVERIFY(!parseObject(fileWithKeys({QJsonObject{{"key", 1.5}}})).config.has_value());
        QVERIFY(!parseObject(fileWithKeys({QJsonObject{{"key", "3"}}})).config.has_value());
        QVERIFY(!parseObject(fileWithKeys({QJsonObject{{"label", "no key number"}}})).config.has_value());
        QVERIFY(mentions(parseObject(fileWithKeys({keyObject(4), keyObject(4)})), "already used"));
    }

    void moreThanTwelveKeysAreRefused()
    {
        QJsonArray keys;
        for (int i = 0; i < 13; ++i)
            keys.append(keyObject(i % 12));
        QVERIFY(!parseObject(fileWithKeys(keys)).config.has_value());
    }

    void labelsAreLimited()
    {
        QVERIFY(parseObject(fileWithKeys({QJsonObject{{"key", 0}, {"label", QString(MaxLabelLength, QLatin1Char('x'))}}})).config.has_value());
        QVERIFY(!parseObject(fileWithKeys({QJsonObject{{"key", 0}, {"label", QString(MaxLabelLength + 1, QLatin1Char('x'))}}})).config.has_value());
        QVERIFY(!parseObject(fileWithKeys({QJsonObject{{"key", 0}, {"label", 12}}})).config.has_value());
    }

    void iconsCannotNamePaths()
    {
        QVERIFY(parseObject(fileWithKeys({keyObject(0, "media-playback-stop")})).config.has_value());
        QVERIFY(parseObject(fileWithKeys({keyObject(0, "my.icon_2-x.png")})).config.has_value());
        for (const QString &icon : {QStringLiteral("../secret.png"), QStringLiteral("/etc/passwd"), QStringLiteral("a/b.png"),
                                   QStringLiteral(""), QStringLiteral("a b"), QStringLiteral("x\\y"), QString(65, QLatin1Char('a'))})
            QVERIFY2(!parseObject(fileWithKeys({keyObject(0, icon)})).config.has_value(), qPrintable(icon));
    }

    void iconStatesAreChecked()
    {
        const auto states = [](const QJsonObject &icon) {
            return parseObject(fileWithKeys({QJsonObject{{"key", 0}, {"icon", icon}}}));
        };
        QVERIFY(states({{"playing", "a"}, {"paused", "b"}, {"stopped", "c"}}).config.has_value());
        QVERIFY(states({{"playing", "a"}}).config.has_value());
        QVERIFY(!states({}).config.has_value());
        QVERIFY(!states({{"buffering", "a"}}).config.has_value());
        QVERIFY(!states({{"playing", "../a"}}).config.has_value());
    }

    void actionsAreChecked()
    {
        const auto withAction = [](const QJsonObject &action) {
            return parseObject(fileWithKeys({QJsonObject{{"key", 0}, {"action", action}}}));
        };
        QVERIFY(withAction({{"type", "media"}, {"command", "stop"}}).config.has_value());
        QVERIFY(withAction({{"type", "media"}, {"command", "volume"}, {"step", -100}}).config.has_value());
        QVERIFY(withAction({{"type", "media"}, {"command", "seek"}, {"seconds", 3600}}).config.has_value());
        QVERIFY(withAction({{"type", "back"}}).config.has_value());
        QVERIFY(withAction({{"type", "none"}}).config.has_value());

        QVERIFY(!withAction({{"type", "shell"}, {"command", "rm -rf ~"}}).config.has_value()); // no command execution
        QVERIFY(!withAction({{"type", "media"}, {"command", "explode"}}).config.has_value());
        QVERIFY(!withAction({{"type", "media"}}).config.has_value());
        QVERIFY(!withAction({{"command", "stop"}}).config.has_value());
        QVERIFY(!withAction({{"type", "media"}, {"command", "volume"}}).config.has_value());               // no step
        QVERIFY(!withAction({{"type", "media"}, {"command", "volume"}, {"step", 0}}).config.has_value());
        QVERIFY(!withAction({{"type", "media"}, {"command", "volume"}, {"step", 101}}).config.has_value());
        QVERIFY(!withAction({{"type", "media"}, {"command", "volume"}, {"step", 2.5}}).config.has_value());
        QVERIFY(!withAction({{"type", "media"}, {"command", "seek"}, {"seconds", 0}}).config.has_value());
        QVERIFY(!withAction({{"type", "media"}, {"command", "seek"}, {"seconds", 3601}}).config.has_value());
        QVERIFY(!withAction({{"type", "media"}, {"command", "stop"}, {"seconds", 5}}).config.has_value()); // stray field
        QVERIFY(!withAction({{"type", "page"}}).config.has_value());
        QVERIFY(!withAction({{"type", "page"}, {"page", "missing"}}).config.has_value());
        QVERIFY(!withAction({{"type", "back"}, {"page", "p"}}).config.has_value());
    }

    void anApplicationActionNamesADesktopFile()
    {
        const auto withAction = [](const QJsonObject &action) {
            return parseObject(fileWithKeys({QJsonObject{{"key", 0}, {"action", action}}}));
        };
        auto ok = withAction({{"type", "app"}, {"desktop", "firefox.desktop"}});
        QVERIFY2(ok.errors.isEmpty(), qPrintable(ok.errors.join('\n')));
        QCOMPARE(int(ok.config->pages.value("p").keys[0]->action.type), int(ActionType::App));
        QCOMPARE(ok.config->pages.value("p").keys[0]->action.desktop, QStringLiteral("firefox.desktop"));
        QVERIFY(withAction({{"type", "app"}, {"desktop", "/usr/share/applications/vlc.desktop"}}).config.has_value());

        // No command can be written here: only the name of a .desktop file.
        QVERIFY(!withAction({{"type", "app"}, {"command", "rm -rf ~"}}).config.has_value());
        QVERIFY(!withAction({{"type", "app"}, {"desktop", "firefox.desktop"}, {"command", "x"}}).config.has_value());
        QVERIFY(!withAction({{"type", "app"}}).config.has_value());
        QVERIFY(!withAction({{"type", "app"}, {"desktop", "firefox"}}).config.has_value());
        QVERIFY(!withAction({{"type", "app"}, {"desktop", "../../bin/sh.desktop"}}).config.has_value());
        QVERIFY(!withAction({{"type", "app"}, {"desktop", "relative/path.desktop"}}).config.has_value());
        QVERIFY(!withAction({{"type", "app"}, {"desktop", 7}}).config.has_value());
        QVERIFY(!withAction({{"type", "app"}, {"desktop", QString(300, QLatin1Char('a')) + ".desktop"}}).config.has_value());
    }

    void aDialTurnCannotLaunchAnApplication()
    {
        QJsonObject object = fileWithKeys({});
        object.insert("dials", QJsonObject{{"0", QJsonObject{{"turn", QJsonObject{{"type", "app"}, {"desktop", "a.desktop"}}}}}});
        QVERIFY(!parseObject(object).config.has_value());
        object.insert("dials", QJsonObject{{"0", QJsonObject{{"press", QJsonObject{{"type", "app"}, {"desktop", "a.desktop"}}}}}});
        QVERIFY(parseObject(object).config.has_value()); // a press may
    }

    void dialsAreChecked()
    {
        const auto withDials = [](const QJsonObject &dials) {
            QJsonObject object = fileWithKeys({});
            object.insert("dials", dials);
            return parseObject(object);
        };
        const QJsonObject volume{{"type", "media"}, {"command", "volume"}, {"step", 3}};
        QVERIFY(withDials({{"0", QJsonObject{{"turn", volume}}}}).config.has_value());
        QVERIFY(withDials({}).config.has_value());
        QVERIFY(!withDials({{"2", QJsonObject{{"turn", volume}}}}).config.has_value());
        QVERIFY(!withDials({{"0", QJsonObject{{"turn", QJsonObject{{"type", "media"}, {"command", "next"}}}}}}).config.has_value());
        QVERIFY(!withDials({{"0", QJsonObject{{"turn", QJsonObject{{"type", "back"}}}}}}).config.has_value());
        QVERIFY(!withDials({{"0", QJsonObject{{"spin", volume}}}}).config.has_value());
        QVERIFY(withDials({{"1", QJsonObject{{"press", QJsonObject{{"type", "media"}, {"command", "next"}}}}}}).config.has_value());
    }

    void everyProblemIsReportedAtOnce()
    {
        const auto result = parseObject(fileWithKeys({keyObject(12), keyObject(0, "../x"), QJsonObject{{"key", 1}, {"action", QJsonObject{{"type", "zap"}}}}}));
        QVERIFY(!result.config.has_value());
        QVERIFY(result.errors.size() >= 3);
        QVERIFY(mentions(result, "keys[0].key"));
        QVERIFY(mentions(result, "keys[1].icon"));
        QVERIFY(mentions(result, "keys[2].action.type"));
    }

    void theScreenSectionIsOptionalAndChecked()
    {
        const PagesConfig defaults = *parsePagesConfig(defaultPagesJson()).config;
        QCOMPARE(int(defaults.screen.nowPlaying), int(NowPlayingMode::Playing));
        QVERIFY(defaults.screen.remoteCovers);

        const auto withScreen = [](const QJsonValue &screen) {
            QJsonObject object = defaultObject();
            object.insert("screen", screen);
            return parseObject(object);
        };
        auto result = withScreen(QJsonObject{{"now_playing", "playing-or-paused"}, {"remote_covers", false}});
        QVERIFY2(result.errors.isEmpty(), qPrintable(result.errors.join('\n')));
        QCOMPARE(int(result.config->screen.nowPlaying), int(NowPlayingMode::PlayingOrPaused));
        QVERIFY(!result.config->screen.remoteCovers);
        QCOMPARE(int(withScreen(QJsonObject{{"now_playing", "never"}}).config->screen.nowPlaying), int(NowPlayingMode::Never));
        QVERIFY(withScreen(QJsonObject{}).config.has_value());

        QVERIFY(defaults.screen.videoThumbnails);
        QVERIFY(!withScreen(QJsonObject{{"video_thumbnails", false}}).config->screen.videoThumbnails);
        QVERIFY(!withScreen(QJsonObject{{"video_thumbnails", "no"}}).config.has_value());
        QVERIFY(!withScreen(QJsonObject{{"now_playing", "sometimes"}}).config.has_value());
        QVERIFY(!withScreen(QJsonObject{{"now_playing", 1}}).config.has_value());
        QVERIFY(!withScreen(QJsonObject{{"remote_covers", "yes"}}).config.has_value());
        QVERIFY(!withScreen(QJsonObject{{"colour", "red"}}).config.has_value());
        QCOMPARE(withScreen(QJsonObject{}).config->screen.wallpaperHoldSeconds, 4);
        QCOMPARE(withScreen(QJsonObject{{"wallpaper_hold_seconds", 12}}).config->screen.wallpaperHoldSeconds, 12);
        QCOMPARE(withScreen(QJsonObject{}).config->screen.wallpaperStepDelayMs, 150);
        QCOMPARE(withScreen(QJsonObject{{"wallpaper_step_delay_ms", 0}}).config->screen.wallpaperStepDelayMs, 0);
        QVERIFY(!withScreen(QJsonObject{{"wallpaper_step_delay_ms", -1}}).config.has_value());
        QVERIFY(!withScreen(QJsonObject{{"wallpaper_step_delay_ms", 5001}}).config.has_value());
        QVERIFY(!withScreen(QJsonObject{{"wallpaper_hold_seconds", 0}}).config.has_value());
        QVERIFY(!withScreen(QJsonObject{{"wallpaper_hold_seconds", 601}}).config.has_value());
        QVERIFY(!withScreen(QJsonObject{{"wallpaper_hold_seconds", "long"}}).config.has_value());
        QVERIFY(!withScreen("playing").config.has_value());
    }

    void themesAreOptionalAndChecked()
    {
        const auto withThemes = [](const QJsonValue &themes) {
            return parseEdited([&](QJsonObject &object) { object.insert("themes", themes); });
        };
        const auto theme = [](const QString &name, const QString &page, const QString &color) {
            return QJsonObject{{"name", name}, {"page", page}, {"color", color}};
        };
        QVERIFY(parseEdited([](QJsonObject &) {}).config->themes.empty());

        const auto good = withThemes(QJsonArray{theme("Control", "main", "#ffffff"), theme("Sound", "main", "#E53935")});
        QVERIFY2(good.config.has_value(), qPrintable(good.errors.join('\n')));
        QCOMPARE(good.config->themes.size(), size_t(2));
        QCOMPARE(good.config->themes[1].rgb, 0xE53935u);
        QCOMPARE(good.config->themes[0].page, QStringLiteral("main"));

        QVERIFY(!withThemes(QJsonArray{theme("X", "nowhere", "#ffffff")}).config.has_value()); // unknown page
        QVERIFY(!withThemes(QJsonArray{theme("X", "main", "white")}).config.has_value());
        QVERIFY(!withThemes(QJsonArray{theme("X", "main", "#fff")}).config.has_value());
        QVERIFY(!withThemes(QJsonArray{theme("X", "main", "#ggggggg")}).config.has_value());
        QVERIFY(!withThemes(QJsonArray{theme("", "main", "#ffffff")}).config.has_value());
        QVERIFY(!withThemes(QJsonArray{theme(QString(MaxLabelLength + 1, 'x'), "main", "#ffffff")}).config.has_value());
        QVERIFY(!withThemes(QJsonArray{QJsonObject{{"name", "X"}, {"page", "main"}}}).config.has_value());
        QVERIFY(!withThemes(QJsonArray{QJsonObject{{"name", "X"}, {"page", "main"}, {"color", "#ffffff"}, {"icon", "x"}}}).config.has_value());
        QVERIFY(!withThemes("Sound").config.has_value());
        QVERIFY(!withThemes(QJsonArray{"Sound"}).config.has_value());
        QJsonArray tooMany;
        for (int i = 0; i <= MaxThemes; ++i)
            tooMany.append(theme("T", "main", "#ffffff"));
        QVERIFY(!withThemes(tooMany).config.has_value());
    }

    void aThemeActionIsAcceptedOnAPress()
    {
        const auto press = parseEdited([](QJsonObject &object) {
            QJsonObject dials = object.value("dials").toObject();
            QJsonObject dial = dials.value("0").toObject();
            dial.insert("press", QJsonObject{{"type", "theme"}});
            dials.insert("0", dial);
            object.insert("dials", dials);
        });
        QVERIFY2(press.config.has_value(), qPrintable(press.errors.join('\n')));
        QVERIFY(press.config->dials[0].press.type == ActionType::Theme);

        const auto turn = parseEdited([](QJsonObject &object) {
            QJsonObject dials = object.value("dials").toObject();
            QJsonObject dial = dials.value("0").toObject();
            dial.insert("turn", QJsonObject{{"type", "theme"}});
            dials.insert("0", dial);
            object.insert("dials", dials);
        });
        QVERIFY(!turn.config.has_value());
        QVERIFY(!parseEdited([](QJsonObject &object) {
                    QJsonObject dials = object.value("dials").toObject();
                    QJsonObject dial = dials.value("0").toObject();
                    dial.insert("press", QJsonObject{{"type", "theme"}, {"page", "main"}});
                    dials.insert("0", dial);
                    object.insert("dials", dials);
                }).config.has_value());
    }

    void aWallpaperModeActionIsAcceptedOnAPress()
    {
        const auto edit = [](const QJsonObject &action, const QString &field) {
            return parseEdited([&](QJsonObject &object) {
                QJsonObject dials = object.value("dials").toObject();
                QJsonObject dial = dials.value("1").toObject();
                dial.insert(field, action);
                dials.insert("1", dial);
                object.insert("dials", dials);
            });
        };
        const auto press = edit(QJsonObject{{"type", "wallpaper-mode"}}, "press");
        QVERIFY2(press.config.has_value(), qPrintable(press.errors.join('\n')));
        QVERIFY(press.config->dials[1].press.type == ActionType::WallpaperMode);
        QVERIFY(!edit(QJsonObject{{"type", "wallpaper-mode"}}, "turn").config.has_value());
        QVERIFY(!edit(QJsonObject{{"type", "wallpaper-mode"}, {"x", 1}}, "press").config.has_value());
    }

    void keyActionsNameAKeyOfTheFixedList()
    {
        const auto withKey = [](const QJsonObject &action) {
            return parseEdited([&](QJsonObject &object) {
                QJsonObject pages = object.value("pages").toObject();
                QJsonObject main = pages.value("main").toObject();
                QJsonArray keys = main.value("keys").toArray();
                keys.append(QJsonObject{{"key", 4}, {"label", "7"}, {"action", action}});
                main.insert("keys", keys);
                pages.insert("main", main);
                object.insert("pages", pages);
            });
        };
        const auto good = withKey({{"type", "key"}, {"key", "kp7"}});
        QVERIFY2(good.config.has_value(), qPrintable(good.errors.join('\n')));
        QVERIFY(withKey({{"type", "key"}, {"key", "kpenter"}}).config.has_value());
        QVERIFY(!withKey({{"type", "key"}, {"key", "a"}}).config.has_value());
        QVERIFY(!withKey({{"type", "key"}, {"key", "ctrl"}}).config.has_value());
        QVERIFY(!withKey({{"type", "key"}, {"key", 7}}).config.has_value());
        QVERIFY(!withKey({{"type", "key"}}).config.has_value());
        QVERIFY(!withKey({{"type", "key"}, {"key", "kp7"}, {"code", 71}}).config.has_value());
    }

    void aThemeCanBeChosenByTheMediaState()
    {
        const auto withThemes = [](const QJsonArray &themes) {
            return parseEdited([&](QJsonObject &object) { object.insert("themes", themes); });
        };
        const auto theme = [](const QString &name, const QString &when) {
            QJsonObject t{{"name", name}, {"page", "main"}, {"color", "#ffffff"}};
            if (!when.isEmpty())
                t.insert("when", when);
            return t;
        };
        const auto good = withThemes({theme("A", "media"), theme("B", "no-media"), theme("C", "")});
        QVERIFY2(good.config.has_value(), qPrintable(good.errors.join('\n')));
        QVERIFY(good.config->themes[0].when == ThemeTrigger::Media);
        QVERIFY(good.config->themes[1].when == ThemeTrigger::NoMedia);
        QVERIFY(good.config->themes[2].when == ThemeTrigger::None);
        QVERIFY(!withThemes({theme("A", "media"), theme("B", "media")}).config.has_value()); // one per trigger
        QVERIFY(!withThemes({theme("A", "sometimes")}).config.has_value());
    }

    void aPageCanSortByUsageAndFillWithSteamGames()
    {
        const auto withPage = [](const QJsonObject &extra) {
            return parseEdited([&](QJsonObject &object) {
                QJsonObject pages = object.value("pages").toObject();
                QJsonObject main = pages.value("main").toObject();
                for (auto it = extra.begin(); it != extra.end(); ++it)
                    main.insert(it.key(), it.value());
                pages.insert("main", main);
                object.insert("pages", pages);
            });
        };
        const auto good = withPage({{"sort", "usage"}, {"steam", QJsonObject{{"max", 6}}}});
        QVERIFY2(good.config.has_value(), qPrintable(good.errors.join('\n')));
        const Page &page = good.config->pages.value("main");
        QVERIFY(page.sort == PageSort::Usage);
        QCOMPARE(page.steamMax, 6);
        QVERIFY(!withPage({{"sort", "random"}}).config.has_value());
        QVERIFY(!withPage({{"sort", 1}}).config.has_value());
        QVERIFY(!withPage({{"steam", QJsonObject{{"max", 0}}}}).config.has_value());
        QVERIFY(!withPage({{"steam", QJsonObject{{"max", 13}}}}).config.has_value());
        QVERIFY(!withPage({{"steam", QJsonObject{{"count", 3}}}}).config.has_value());
        QVERIFY(!withPage({{"steam", 6}}).config.has_value());
    }

    void aSteamActionNamesAGameByItsApplicationNumber()
    {
        const auto withKey = [](const QJsonObject &action) {
            return parseEdited([&](QJsonObject &object) {
                QJsonObject pages = object.value("pages").toObject();
                QJsonObject main = pages.value("main").toObject();
                QJsonArray keys = main.value("keys").toArray();
                keys.append(QJsonObject{{"key", 4}, {"label", "CS"}, {"action", action}});
                main.insert("keys", keys);
                pages.insert("main", main);
                object.insert("pages", pages);
            });
        };
        const auto good = withKey({{"type", "steam"}, {"appid", 730}});
        QVERIFY2(good.config.has_value(), qPrintable(good.errors.join('\n')));
        QVERIFY(withKey({{"type", "steam"}, {"appid", 2000000000}}).config.has_value());
        QVERIFY(!withKey({{"type", "steam"}, {"appid", 0}}).config.has_value());
        QVERIFY(!withKey({{"type", "steam"}, {"appid", -3}}).config.has_value());
        QVERIFY(!withKey({{"type", "steam"}, {"appid", 2000000001}}).config.has_value());
        QVERIFY(!withKey({{"type", "steam"}, {"appid", "730"}}).config.has_value());
        QVERIFY(!withKey({{"type", "steam"}, {"appid", 730.5}}).config.has_value());
        QVERIFY(!withKey({{"type", "steam"}}).config.has_value());
        QVERIFY(!withKey({{"type", "steam"}, {"appid", 730}, {"command", "x"}}).config.has_value());
    }

    void errorsAndConfigAreMutuallyExclusive()
    {
        // Every single-byte change of the default file: never a crash, and
        // never both (or neither) a config and errors.
        const QByteArray original = defaultPagesJson();
        const QByteArray values("\0\"{}[]:,0123456789xX-. \n\\", 26);
        for (qsizetype at = 0; at < original.size(); ++at) {
            for (const char value : values) {
                QByteArray changed = original;
                changed[at] = value;
                const auto result = parsePagesConfig(changed);
                QVERIFY2(result.config.has_value() == result.errors.isEmpty(), qPrintable(QString::number(at)));
            }
        }
        for (qsizetype n = 0; n < original.size(); n += 7) // truncations
            QVERIFY(parsePagesConfig(original.left(n)).config.has_value() == parsePagesConfig(original.left(n)).errors.isEmpty());
    }
};

QTEST_GUILESS_MAIN(TestPagesConfig)
#include "test_pagesconfig.moc"
