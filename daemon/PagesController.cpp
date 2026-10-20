// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include <InitialPages.h>

#include "PagesController.h"

#include "PageOrder.h"

#include <algorithm>
#include <iterator>
#include <utility>
#include <vector>

#include <galleon/StreamDeck.h>

#include <QDebug>
#ifdef Q_OS_WIN
#include <QDesktopServices>
#include <QUrl>
#endif
#include <QDir>
#include <QFileDevice>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QStandardPaths>

#include "FileTrust.h"

namespace galleond {

namespace {

constexpr int ReloadDelayMs = 250; // editors write a file in several steps
constexpr int MinLaunchIntervalMs = 700; // a double press must not start an application twice
constexpr int MaxLookedUpApps = 64;

bool isMediaPresent(MediaController::State state)
{
    return state == MediaController::State::Playing || state == MediaController::State::Paused;
}
constexpr int ThemeApplyMs = 30; // rest between two themes drawn: a theme takes 13 pictures to show
constexpr int ModeTimeoutMs = 30000; // back to the player control when left alone
constexpr std::uint32_t WallpaperModeRgb = 0xff00ff; // magenta

} // namespace

PagesController::PagesController(galleon::StreamDeck *deck, MediaController *media, TileRenderer tiles, QObject *parent)
    : QObject(parent)
    , m_deck(deck)
    , m_media(media)
    , m_tiles(std::move(tiles))
    , m_mediaPresent(isMediaPresent(m_media->state())), m_launcher([](const DesktopApp &app) { return DesktopEntries::launch(app); })
    , m_steamLauncher([](int appid) {
        // A fixed program and one argument made of a whole number: nothing from outside goes in.
#ifdef Q_OS_WIN
        return QDesktopServices::openUrl(QUrl(QStringLiteral("steam://rungameid/%1").arg(appid)));
#else
        const QString steam = QStandardPaths::findExecutable(QStringLiteral("steam"));
        return !steam.isEmpty()
               && QProcess::startDetached(steam, {QStringLiteral("steam://rungameid/%1").arg(appid)});
#endif
    })
    , m_sourceProvider([](const QString &menuCategory) {
        ArrangeSources sources;
        sources.installedSteam = SteamLibrary::installedGames();
        sources.playedSteam = SteamLibrary::playedGames();
        if (!menuCategory.isEmpty()) {
            sources.menu = DesktopEntries::listCategory(menuCategory);
            // The Steam game behind each shortcut of the whole menu, to score it by play time.
            for (const DesktopApp &app : DesktopEntries::list()) {
                if (app.steamAppId > 0)
                    sources.steamOfApp.insert(app.id, app.steamAppId);
            }
        }
        return sources;
    })
{
    m_clock.start();
    m_lastPressAt.fill(-static_cast<qint64>(ClickAfterMs) * 4);
    
    // The built-in pages: always valid, so there is always something to show.
    if (const auto fallback = parsePagesConfig(initialPagesJson()); fallback.config)
        setConfig(*fallback.config);

    connect(m_deck, &galleon::StreamDeck::keyPressed, this, &PagesController::onKeyPressed);
    connect(m_deck, &galleon::StreamDeck::keyReleased, this, &PagesController::onKeyReleased);
    connect(m_deck, &galleon::StreamDeck::dialRotated, this, &PagesController::onDialRotated);
    connect(m_deck, &galleon::StreamDeck::dialPressed, this, &PagesController::onDialPressed);
    connect(m_deck, &galleon::StreamDeck::dialReleased, this, &PagesController::onDialReleased);
    connect(m_media, &MediaController::stateChanged, this, [this] {
        onMediaChanged();
        redraw();
    });

    m_wallpaperTimeout.setInterval(1000);
    connect(&m_wallpaperTimeout, &QTimer::timeout, this, [this] {
        // The countdown: the dial stays in the choice, magenta, until it is over.
        if (m_wallpaperLeft > 1) {
            --m_wallpaperLeft;
            qInfo() << "galleond: validation in" << m_wallpaperLeft << "s";
            Q_EMIT wallpaperCountdown(m_wallpaperLeft);
        } else {
            m_wallpaperTimeout.stop();
            setWallpaperMode(-1); // validated: the ring goes white and the choice is closed
        }
    });
    // If the screen never says the label is shown, the turns count after a moment all the same.
    m_wallpaperReadyFallback.setSingleShot(true);
    m_wallpaperReadyFallback.setInterval(1500);
    connect(&m_wallpaperReadyFallback, &QTimer::timeout, this, [this] {
        m_wallpaperReady = true;
        m_wallpaperBusy = false;
        m_lastWallpaperStepAt = m_clock.elapsed();
    });

    m_themeApply.setSingleShot(true);
    m_themeApply.setInterval(ThemeApplyMs);
    connect(&m_themeApply, &QTimer::timeout, this, [this] {
        if (!m_themePending)
            return;
        applyTheme();
        m_themeApply.start(); // rest again before the next one
    });
    m_themeTimeout.setSingleShot(true);
    m_themeTimeout.setInterval(ModeTimeoutMs);
    connect(&m_themeTimeout, &QTimer::timeout, this, [this] { setThemeMode(-1); });

    m_reloadTimer.setSingleShot(true);
    m_reloadTimer.setInterval(ReloadDelayMs);
    connect(&m_reloadTimer, &QTimer::timeout, this, &PagesController::reload);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_reloadTimer, qOverload<>(&QTimer::start));
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_reloadTimer, qOverload<>(&QTimer::start));
}

void PagesController::setConfig(const PagesConfig &config)
{
    m_apps.clear();
    const bool screenChanged = !(config.screen == m_config.screen);
    const bool saverChanged = !(config.screensaver == m_config.screensaver);
    m_config = config;
    releaseHeldKeys();
    m_stack = {config.home};
    m_theme = -1;
    m_sourcesAge.invalidate();
    for (std::size_t i = 0; i < config.themes.size(); ++i) {
        if (config.themes[i].page == config.home) {
            m_theme = static_cast<int>(i);
            break;
        }
    }
    // A theme tied to the player's presence wins over the home page.
    if (const int automatic = themeFor(isMediaPresent(m_media->state()) ? ThemeTrigger::Media : ThemeTrigger::NoMedia);
        automatic >= 0) {
        m_theme = automatic;
        m_stack = {config.themes[static_cast<std::size_t>(automatic)].page};
    }
    if (m_injector && hasKeyActions(config))
        m_injector->prepare();
    rebuildPage();
    m_pressed.fill(false);
    m_wallpaperDial = -1; // a new file: back to the player control
    m_wallpaperTimeout.stop();
    Q_EMIT wallpaperCountdown(0);
    m_themeDial = -1;
    m_themeTimeout.stop();
    redraw(true);
    applyRings();
    Q_EMIT themeChanged();
    Q_EMIT wallpaperModeChanged();
    Q_EMIT themeModeChanged();
    if (screenChanged)
        Q_EMIT screenConfigChanged();
    if (saverChanged)
        Q_EMIT screensaverConfigChanged();
}

void PagesController::loadFile(const QString &path)
{
    m_file = path;
    reload();
}

void PagesController::watch(const QString &path)
{
    // A file that an editor replaces drops out of the watch list: add it again
    // after each change, and watch its folder to see it appear.
    const QFileInfo info(path);
    const QStringList watched = m_watcher.files() + m_watcher.directories();
    if (info.exists() && !watched.contains(info.absoluteFilePath()))
        m_watcher.addPath(info.absoluteFilePath());
    if (QDir(info.absolutePath()).exists() && !watched.contains(info.absolutePath()))
        m_watcher.addPath(info.absolutePath());
}

void PagesController::reload()
{
    if (m_file.isEmpty())
        return;
    watch(m_file);

    QFile file(m_file);
    if (!file.exists()) {
        qInfo() << "galleond: no pages file at" << m_file << "- the built-in pages are used";
        return;
    }
    // Read one byte more than the limit: parsePagesConfig refuses a longer file.
    if (!file.open(QIODevice::ReadOnly)) {
        qWarning() << "galleond: cannot read the pages file" << m_file << file.errorString();
        return;
    }
    // Applications are started from this file, so it must be the user's own and
    // not writable by anyone else (as for a shell startup file).
    const bool owned = isTrustedPagesFile(file);
    if (!owned && m_appsAllowed)
        qWarning() << "galleond: the pages file" << m_file << "is not owned by you or is writable by others: its application keys are disabled";
    m_appsAllowed = owned;
    const QByteArray bytes = preparePagesJson(file.read(MaxPagesFileBytes + 1));
    const PagesParseResult result = parsePagesConfig(bytes);
    if (!result.config) {
        qWarning() << "galleond: the pages file" << m_file << "has errors and is not used:";
        for (const QString &line : result.errors)
            qWarning().noquote() << "   " << line;
        return;
    }
    if (*result.config == m_config)
        return;
    qInfo() << "galleond: pages loaded from" << m_file;
    setConfig(*result.config);
}

const Page *PagesController::effectivePage() const
{
    if (m_dynamic && m_dynamicFor == currentPage())
        return &*m_dynamic;
    const auto page = m_config.pages.constFind(currentPage());
    return page == m_config.pages.constEnd() ? nullptr : &*page;
}

const KeyConfig *PagesController::keyConfig(int key) const
{
    const Page *page = effectivePage();
    if (!page || key < 0 || key >= galleon::protocol::KeyCount)
        return nullptr;
    const auto &configured = page->keys.at(static_cast<std::size_t>(key));
    return configured ? &*configured : nullptr;
}

void PagesController::setUsageFile(const QString &file)
{
    m_usage = std::make_unique<UsageStats>(file);
    rebuildPage();
}

void PagesController::recordUse(const QString &id)
{
    m_usage->record(id);
    rebuildPage(); // the most used may have changed
    redraw();
}

void PagesController::rebuildPage()
{
    m_dynamic.reset();
    m_highlightKey = -1;
    const auto found = m_config.pages.constFind(currentPage());
    if (found == m_config.pages.constEnd()
        || (found->sort == PageSort::None && found->steamMax == 0 && found->menuMax == 0))
        return;

    // Steam's files and the desktop menu are read when a page needs them, then kept for five minutes.
    const bool needsSources = found->steamMax > 0 || found->menuMax > 0 || found->sort == PageSort::Usage;
    if (needsSources && (!m_sourcesAge.isValid() || m_sourcesAge.elapsed() > 300000 || m_sourcesFor != found->menuCategory)) {
        m_sources = m_sourceProvider ? m_sourceProvider(found->menuMax > 0 ? found->menuCategory : QString()) : ArrangeSources{};
        m_sourcesFor = found->menuCategory;
        m_sourcesAge.start();
    }
    const ArrangedPage arranged = arrangePage(*found, *m_usage, m_sources);
    m_dynamic = arranged.page;
    m_highlightKey = arranged.highlightKey;
    m_dynamicFor = currentPage();
}

void PagesController::drawKey(int key, bool force)
{
    if (!m_deck->isOpen())
        return;
    const auto index = static_cast<std::size_t>(key);
    const KeyConfig *configured = keyConfig(key);

    const Theme *theme = currentTheme();
    const qint64 tint = theme ? static_cast<qint64>(theme->rgb) : -1;

    QImage image;
    if (configured) {
        QString iconFile; // an application's icon given by path
        const KeyConfig effective = TileRenderer::effectiveKey(
            *configured, configured->action.type == ActionType::App ? appFor(configured->action.desktop) : DesktopApp{}, iconFile);
        if (configured->action.type == ActionType::Steam && configured->icon.name.isEmpty())
            iconFile = SteamLibrary::coverImage(configured->action.appid); // the game's cover from Steam's own cache
        // Shuffle and repeat show their state in words, and a lighter key when it is on.
        KeyConfig shown = effective;
        bool active = false;
        if (configured->action.type == ActionType::Media
            && (configured->action.command == MediaCommand::Shuffle || configured->action.command == MediaCommand::Repeat)) {
            const bool shuffle = configured->action.command == MediaCommand::Shuffle;
            int level = 0; // shuffle: off, on; repeat: none, the playlist, the track
            if (shuffle)
                level = m_media->shuffleOn() ? 1 : 0;
            else if (m_media->loopMode() == MediaController::Loop::Playlist)
                level = 1;
            else if (m_media->loopMode() != MediaController::Loop::None)
                level = 2;
            active = level > 0;
            QStringList words = configured->stateLabels;
            if (words.isEmpty())
                words = shuffle ? QStringList{QStringLiteral("off"), QStringLiteral("on")}
                                : QStringList{QStringLiteral("off"), QStringLiteral("list"), QStringLiteral("track")};
            // Two words for a three-state key: off, and on.
            const QString word = words.value(std::min<qsizetype>(level, words.size() - 1));
            shown.label = (configured->label.isEmpty() ? word : configured->label + QLatin1Char(' ') + word).left(MaxLabelLength);
        }
        image = m_tiles.render(shown, MediaController::stateName(m_media->state()), m_pressed.at(index), iconFile, tint,
                               key == m_highlightKey, active);
    } else {
        image = TileRenderer::blank(tint);
    }

    if (!force && image == m_sent.at(index))
        return;
    // A failed send is forgotten, so that the next redraw tries again.
    const bool sent = m_deck->setKeyImage(key, image);
    if (!sent && (m_failedSends++ % 20) == 0) // said now and then, not at every key
        qWarning() << "galleond: the picture of key" << key << "could not be sent to the module";
    m_sent.at(index) = sent ? image : QImage();
}

void PagesController::setSuspended(bool suspended)
{
    if (suspended == m_suspended)
        return;
    m_suspended = suspended;
    m_pressed.fill(false);
    releaseHeldKeys();
    if (suspended) {
        for (int dial = 0; dial < galleon::protocol::DialCount; ++dial)
            m_deck->setDialRing(dial, 0, 0, 0);
        return;
    }
    redraw(true);
    applyRings();
}

void PagesController::redraw(bool force)
{
    if (m_suspended)
        return;
    for (int key = 0; key < galleon::protocol::KeyCount; ++key)
        drawKey(key, force);
}

void PagesController::perform(const Action &action, int times)
{
    switch (action.type) {
    case ActionType::None:
        return;
    case ActionType::Page:
        if (m_config.pages.contains(action.page) && m_stack.size() < MaxPageDepth) {
            m_stack.append(action.page);
            rebuildPage();
            m_pressed.fill(false);
            releaseHeldKeys();
            redraw();
        }
        return;
    case ActionType::Back:
        if (m_stack.size() > 1) {
            m_stack.removeLast();
            rebuildPage();
            m_pressed.fill(false);
            releaseHeldKeys();
            redraw();
        }
        return;
    case ActionType::App:
        launchApp(action.desktop);
        return;
    case ActionType::Steam:
        launchSteam(action.appid);
        return;
    case ActionType::WallpaperMode:
        setWallpaperMode(m_pressedDial);
        return;
    case ActionType::Key:
        // From a key press the key is held (see onKeyPressed); here, a tap.
        if (m_injector) {
            if (const auto code = keyCodeForName(action.keyName)) {
                m_injector->key(*code, true);
                m_injector->key(*code, false);
            }
        }
        return;
    case ActionType::Theme:
        if (m_pressedDial >= 0)
            setThemeMode(m_pressedDial); // a dial: choose the theme by turning it
        else
            stepTheme(1);
        return;
    case ActionType::Media:
        break;
    }

    switch (action.command) {
    case MediaCommand::PlayPause:
        m_media->playPause();
        break;
    case MediaCommand::Next:
        m_media->next();
        break;
    case MediaCommand::Previous:
        m_media->previous();
        break;
    case MediaCommand::Stop:
        m_media->stop();
        break;
    case MediaCommand::Shuffle:
        m_media->toggleShuffle();
        break;
    case MediaCommand::Repeat:
        m_media->cycleRepeat();
        break;
    case MediaCommand::Mute:
        m_media->toggleMute();
        break;
    case MediaCommand::Raise:
        m_media->raisePlayer();
        break;
    case MediaCommand::Fullscreen:
        m_media->toggleFullscreen();
        break;
    case MediaCommand::Restart:
        m_media->restartTrack();
        break;
    case MediaCommand::Volume:
        m_media->changeVolume(action.step * times);
        break;
    case MediaCommand::Seek:
        m_media->seek(action.seconds * times);
        break;
    }
}

int PagesController::themeFor(ThemeTrigger trigger) const
{
    for (std::size_t i = 0; i < m_config.themes.size(); ++i) {
        if (m_config.themes[i].when == trigger)
            return static_cast<int>(i);
    }
    return -1;
}

void PagesController::onMediaChanged()
{
    const bool present = isMediaPresent(m_media->state());
    if (present == m_mediaPresent)
        return;
    m_mediaPresent = present;
    // The theme tied to this moment, unless the user is choosing one right now.
    const int index = themeFor(present ? ThemeTrigger::Media : ThemeTrigger::NoMedia);
    if (index >= 0 && index != m_theme && !themeMode() && !m_suspended)
        selectTheme(index);
}

bool PagesController::hasKeyActions(const PagesConfig &config)
{
    const auto isKey = [](const KeyConfig &key) { return key.action.type == ActionType::Key; };
    const bool onAKey = std::ranges::any_of(config.pages, [&](const Page &page) {
        return std::ranges::any_of(page.keys, [&](const auto &key) { return key && isKey(*key); });
    });
    return onAKey || std::ranges::any_of(config.dials, [](const DialConfig &dial) { return dial.press.type == ActionType::Key; });
}

void PagesController::releaseHeldKeys()
{
    for (int &code : m_heldKey) {
        if (code != 0 && m_injector)
            m_injector->key(code, false);
        code = 0;
    }
}

const Theme *PagesController::currentTheme() const
{
    if (m_theme < 0 || std::cmp_greater_equal(m_theme, m_config.themes.size()))
        return nullptr;
    return &m_config.themes[static_cast<std::size_t>(m_theme)];
}

void PagesController::stepTheme(int steps)
{
    const auto count = static_cast<int>(m_config.themes.size());
    if (count == 0)
        return;
    // `steps` is bounded by the dial decoder; the modulo keeps any value in range.
    selectTheme((((m_theme < 0 ? -1 : m_theme) + steps) % count + count) % count);
}

void PagesController::restartWallpaperCountdown()
{
    m_wallpaperLeft = std::max(1, m_config.screen.wallpaperValidateSeconds);
    qInfo() << "galleond: validation in" << m_wallpaperLeft << "s (started again)";
    m_wallpaperTimeout.start();
    Q_EMIT wallpaperCountdown(m_wallpaperLeft);
}

void PagesController::wallpaperLabelShown()
{
    if (!wallpaperMode())
        return;
    if (!m_wallpaperReady) {
        m_wallpaperReady = true; // the name was not shown yet: now it is
        m_wallpaperReadyFallback.stop();
        return;
    }
    if (m_wallpaperBusy) {
        // The picture of the last step is on the screen: the rest before the next turn starts now.
        m_wallpaperBusy = false;
        m_lastWallpaperStepAt = m_clock.elapsed();
        m_wallpaperReadyFallback.stop();
    }
}

void PagesController::setThemeMode(int dial)
{
    // A press toggles: the dial that is in the mode leaves it, any other enters it.
    const int wanted = dial >= 0 && dial != m_themeDial && !m_config.themes.empty() ? dial : -1;
    if (wanted == m_themeDial)
        return;
    if (wanted >= 0)
        setWallpaperMode(-1); // one choice at a time: the screen shows one of them
    m_themeDial = wanted;
    qInfo() << "galleond: theme mode" << (themeMode() ? "on, dial" : "off") << (themeMode() ? m_themeDial : -1);
    if (themeMode())
        m_themeTimeout.start();
    else
        m_themeTimeout.stop();
    Q_EMIT themeModeChanged();
}

void PagesController::selectTheme(int index)
{
    m_theme = index;
    qInfo() << "galleond: theme" << m_config.themes[static_cast<std::size_t>(index)].name;
    m_stack = {m_config.themes[static_cast<std::size_t>(index)].page};
    rebuildPage();
    m_pressed.fill(false);
    releaseHeldKeys();
    // The first step is drawn at once (a turn must be answered at once); steps that come
    // while it is being drawn are gathered, and the last theme is drawn when the dial rests.
    if (m_themeApply.isActive()) {
        m_themePending = true;
        return;
    }
    applyTheme();
    m_themeApply.start();
}

void PagesController::applyTheme()
{
    m_themePending = false;
    redraw();
    applyRings();
    Q_EMIT themeChanged();
}

void PagesController::applyRings()
{
    if (!m_deck->isOpen() || m_suspended)
        return;
    // The left ring shows the theme (dark without one); the right ring stays
    // white, for the music player control.
    const Theme *theme = currentTheme();
    const std::uint32_t rgb = theme ? theme->rgb : 0;
    if (!m_deck->setDialRing(0, static_cast<std::uint8_t>(rgb >> 16), static_cast<std::uint8_t>(rgb >> 8),
                            static_cast<std::uint8_t>(rgb)))
        qWarning() << "galleond: could not update the left dial ring";
    // The right ring: white for the player control, magenta while it chooses the wallpaper.
    const std::uint32_t right = wallpaperMode() ? WallpaperModeRgb : 0xffffff;
    if (!m_deck->setDialRing(1, static_cast<std::uint8_t>(right >> 16), static_cast<std::uint8_t>(right >> 8),
                            static_cast<std::uint8_t>(right)))
        qWarning() << "galleond: could not update the right dial ring";
}

void PagesController::setWallpaperMode(int dial)
{
    // A press toggles: the dial that is in the mode leaves it, any other enters it.
    const int wanted = dial >= 0 && dial != m_wallpaperDial ? dial : -1;
    if (wanted == m_wallpaperDial)
        return;
    if (wanted >= 0)
        setThemeMode(-1); // one choice at a time: the screen shows one of them
    m_wallpaperDial = wanted;
    qInfo() << "galleond: wallpaper mode" << (wallpaperMode() ? "on, dial" : "off") << (wallpaperMode() ? m_wallpaperDial : -1);
    if (wallpaperMode()) {
        m_wallpaperReady = false; // turns are ignored until the screen shows the label
        m_wallpaperBusy = false;
        m_wallpaperReadyFallback.start();
        restartWallpaperCountdown();
    } else {
        m_wallpaperTimeout.stop();
        m_wallpaperReadyFallback.stop();
        Q_EMIT wallpaperCountdown(0);
    }
    applyRings();
    Q_EMIT wallpaperModeChanged();
}

DesktopApp PagesController::appFor(const QString &desktop)
{
    if (const auto known = m_apps.constFind(desktop); known != m_apps.constEnd())
        return known.value();
    if (m_apps.size() >= MaxLookedUpApps)
        m_apps.clear();
    const DesktopApp app = DesktopEntries::lookup(desktop);
    m_apps.insert(desktop, app);
    return app;
}

void PagesController::launchApp(const QString &desktop)
{
    if (!m_appsAllowed)
        return;
    if (m_lastLaunch.isValid() && m_lastLaunch.elapsed() < MinLaunchIntervalMs)
        return;
    const DesktopApp app = appFor(desktop);
    if (!app.isValid()) {
        if (!m_warned.value(desktop)) { // once, not at every press
            m_warned.insert(desktop, true);
            qWarning() << "galleond: no application named" << desktop;
        }
        return;
    }
    m_lastLaunch.start();
    if (!m_launcher(app)) {
        qWarning() << "galleond: could not start" << app.name;
        return;
    }
    recordUse(desktop);
}

void PagesController::launchSteam(int appid)
{
    if (!m_appsAllowed || appid <= 0)
        return;
    if (m_lastLaunch.isValid() && m_lastLaunch.elapsed() < MinLaunchIntervalMs)
        return;
    m_lastLaunch.start();
    if (!m_steamLauncher(appid)) {
        qWarning() << "galleond: could not start the Steam game" << appid << "(is steam installed?)";
        return;
    }
    recordUse(QStringLiteral("steam:%1").arg(appid));
}

void PagesController::onKeyPressed(int key)
{
    if (m_suspended)
        return;
    if (key < 0 || key >= galleon::protocol::KeyCount)
        return;
    const KeyConfig *configured = keyConfig(key);
    if (!configured)
        return;
    const Action action = configured->action; // a copy: performing it may change the page
    m_pressed.at(static_cast<std::size_t>(key)) = true;
    drawKey(key, false);
    if (action.type == ActionType::Key) {
        // Held until the key is released, so a long press repeats as on a keyboard.
        if (m_injector) {
            if (const auto code = keyCodeForName(action.keyName); code && m_injector->key(*code, true))
                m_heldKey.at(static_cast<std::size_t>(key)) = *code;
        }
        return;
    }
    perform(action); // may change the page: the pressed flags are reset there
}

void PagesController::onKeyReleased(int key)
{
    if (m_suspended)
        return;
    if (key < 0 || key >= galleon::protocol::KeyCount || !m_pressed.at(static_cast<std::size_t>(key)))
        return;
    m_pressed.at(static_cast<std::size_t>(key)) = false;
    if (int &held = m_heldKey.at(static_cast<std::size_t>(key)); held != 0) {
        if (m_injector)
            m_injector->key(held, false);
        held = 0;
    }
    drawKey(key, false);
}

void PagesController::onDialRotated(int dial, int steps)
{
    if (m_suspended)
        return;
    if (dial < 0 || dial >= galleon::protocol::DialCount || steps == 0)
        return;
    const qint64 now = m_clock.elapsed();
    const auto index = static_cast<std::size_t>(dial);
    if (now - m_lastPressAt.at(index) < ClickAfterMs) {
        qInfo() << "galleond: dial" << dial << "step" << steps << "ignored: it is the click";
        return; // the click's own movement, not a turn
    }
    int counted = steps; // as many places as the dial counts detents
    bool countdownRestarted = false;
    if (dial == m_wallpaperDial && m_config.screen.wallpaperStepDelayMs > 0) {
        // One place at a time, and the next turn is listened to only once the wallpaper
        // of this one is really on the screen (the screen says so, see wallpaperLabelShown)
        // and has been there for a short rest. Turns before that are ignored, not queued:
        // the screen never runs behind the dial, and the index never jumps by several.
        if (!m_wallpaperReady) {
            qInfo() << "galleond: dial" << dial << "step" << steps << "ignored: the wallpaper name is not shown yet";
            return;
        }
        restartWallpaperCountdown(); // the dial is being touched: the choice is not validated yet
        countdownRestarted = true;
        if (m_wallpaperBusy || now - m_lastWallpaperStepAt < m_config.screen.wallpaperStepDelayMs)
            return;
        counted = std::clamp(steps, -1, 1);
        m_wallpaperBusy = true;
        m_wallpaperReadyFallback.start(); // if the screen never answers, listen again after a moment
    }
    auto &recent = m_recentTurns.at(index);
    std::ranges::rotate(recent, std::next(recent.begin())); // the oldest slot is reused
    recent.back() = {.at=now, .steps=counted};
    if (dial == m_themeDial) {
        m_themeTimeout.start(); // in use: keep the mode
        stepTheme(counted);
        return;
    }
    if (dial == m_wallpaperDial) {
        if (!m_wallpaperReady) {
            qInfo() << "galleond: dial" << dial << "step" << steps << "ignored: the wallpaper name is not shown yet";
            return;
        }
        if (!countdownRestarted)
            restartWallpaperCountdown(); // in use: keep the mode
        Q_EMIT wallpaperStep(counted);
        return;
    }
    perform(m_config.dials.at(static_cast<std::size_t>(dial)).turn, steps);
}

void PagesController::onDialReleased(int dial)
{
    if (dial < 0 || dial >= galleon::protocol::DialCount)
        return;
    m_lastPressAt.at(static_cast<std::size_t>(dial)) = m_clock.elapsed(); // letting go moves it too
}

void PagesController::onDialPressed(int dial)
{
    if (m_suspended)
        return;
    if (dial < 0 || dial >= galleon::protocol::DialCount)
        return;
    // A step that came just before this press was the click: take it back.
    const qint64 now = m_clock.elapsed();
    const auto index = static_cast<std::size_t>(dial);
    int wobble = 0;
    for (RecentTurn &turn : m_recentTurns.at(index)) {
        if (turn.steps != 0 && now - turn.at < ClickBeforeMs)
            wobble += turn.steps;
        turn = {};
    }
    m_lastPressAt.at(index) = now;
    if (wobble != 0 && (dial == m_themeDial || dial == m_wallpaperDial))
        qInfo() << "galleond: dial" << dial << "step" << wobble << "taken back: it came with the click";
    if (wobble != 0 && dial == m_themeDial)
        stepTheme(-wobble);
    else if (wobble != 0 && dial == m_wallpaperDial)
        Q_EMIT wallpaperStep(-wobble);
    m_pressedDial = dial;
    perform(m_config.dials.at(static_cast<std::size_t>(dial)).press);
    m_pressedDial = -1;
}

} // namespace galleond
