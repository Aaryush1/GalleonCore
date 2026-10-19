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
