// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#include "PagesController.h"

#include <galleon/StreamDeck.h>

#include <QDebug>
#include <QDir>
#include <QFileDevice>
#include <QFile>
#include <QFileInfo>

#include <unistd.h>

namespace galleond {

namespace {

constexpr int ReloadDelayMs = 250; // editors write a file in several steps
constexpr int MinLaunchIntervalMs = 700; // a double press must not start an application twice
constexpr int MaxLookedUpApps = 64;
constexpr int ModeTimeoutMs = 30000; // back to the player control when left alone
constexpr std::uint32_t WallpaperModeRgb = 0xff00ff; // magenta

} // namespace

PagesController::PagesController(galleon::StreamDeck *deck, MediaController *media, TileRenderer tiles, QObject *parent)
    : QObject(parent)
    , m_deck(deck)
    , m_media(media)
    , m_tiles(std::move(tiles))
    , m_launcher([](const DesktopApp &app) { return DesktopEntries::launch(app); })
{
    // The built-in pages: always valid, so there is always something to show.
    if (const auto fallback = parsePagesConfig(defaultPagesJson()); fallback.config)
        setConfig(*fallback.config);

    connect(m_deck, &galleon::StreamDeck::keyPressed, this, &PagesController::onKeyPressed);
    connect(m_deck, &galleon::StreamDeck::keyReleased, this, &PagesController::onKeyReleased);
    connect(m_deck, &galleon::StreamDeck::dialRotated, this, &PagesController::onDialRotated);
    connect(m_deck, &galleon::StreamDeck::dialPressed, this, &PagesController::onDialPressed);
    connect(m_media, &MediaController::stateChanged, this, [this] { redraw(); });

    m_wallpaperTimeout.setSingleShot(true);
    m_wallpaperTimeout.setInterval(ModeTimeoutMs);
    connect(&m_wallpaperTimeout, &QTimer::timeout, this, [this] { setWallpaperMode(-1); });

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
    m_stack = {config.home};
    m_theme = -1;
    for (std::size_t i = 0; i < config.themes.size(); ++i) {
        if (config.themes[i].page == config.home) {
            m_theme = static_cast<int>(i);
            break;
        }
    }
    m_pressed.fill(false);
    m_wallpaperDial = -1; // a new file: back to the player control
    m_wallpaperTimeout.stop();
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
    const QFileInfo fileInfo(m_file);
    const bool owned = fileInfo.ownerId() == getuid() && !fileInfo.permissions().testFlag(QFileDevice::WriteOther);
    if (!owned && m_appsAllowed)
        qWarning() << "galleond: the pages file" << m_file << "is not owned by you or is writable by others: its application keys are disabled";
    m_appsAllowed = owned;
    const QByteArray bytes = file.read(MaxPagesFileBytes + 1);
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

const KeyConfig *PagesController::keyConfig(int key) const
{
    const auto page = m_config.pages.constFind(currentPage());
    if (page == m_config.pages.constEnd() || key < 0 || key >= galleon::protocol::KeyCount)
        return nullptr;
    const auto &configured = page->keys[static_cast<std::size_t>(key)];
    return configured ? &*configured : nullptr;
}

void PagesController::drawKey(int key, bool force)
{
    if (!m_deck->isOpen())
        return;
    const auto index = static_cast<std::size_t>(key);
    const KeyConfig *configured = keyConfig(key);

    QImage image;
    if (configured) {
        QString iconFile; // an application's icon given by path
        const KeyConfig effective = TileRenderer::effectiveKey(
            *configured, configured->action.type == ActionType::App ? appFor(configured->action.desktop) : DesktopApp{}, iconFile);
        image = m_tiles.render(effective, MediaController::stateName(m_media->state()), m_pressed[index], iconFile);
    } else {
        image = m_tiles.blank();
    }

    if (!force && image == m_sent[index])
        return;
    // A failed send is forgotten, so that the next redraw tries again.
    m_sent[index] = m_deck->setKeyImage(key, image) ? image : QImage();
}

void PagesController::setSuspended(bool suspended)
{
    if (suspended == m_suspended)
        return;
    m_suspended = suspended;
    m_pressed.fill(false);
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
            m_pressed.fill(false);
            redraw();
        }
        return;
    case ActionType::Back:
        if (m_stack.size() > 1) {
            m_stack.removeLast();
            m_pressed.fill(false);
            redraw();
        }
        return;
    case ActionType::App:
        launchApp(action.desktop);
        return;
    case ActionType::WallpaperMode:
        setWallpaperMode(m_pressedDial);
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
    case MediaCommand::Volume:
        m_media->changeVolume(action.step * times);
        break;
    case MediaCommand::Seek:
        m_media->seek(action.seconds * times);
        break;
    }
}

const Theme *PagesController::currentTheme() const
{
    if (m_theme < 0 || m_theme >= static_cast<int>(m_config.themes.size()))
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

void PagesController::setThemeMode(int dial)
{
    // A press toggles: the dial that is in the mode leaves it, any other enters it.
    const int wanted = dial >= 0 && dial != m_themeDial && !m_config.themes.empty() ? dial : -1;
    if (wanted == m_themeDial)
        return;
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
    m_pressed.fill(false);
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
    m_deck->setDialRing(0, static_cast<std::uint8_t>(rgb >> 16), static_cast<std::uint8_t>(rgb >> 8),
                        static_cast<std::uint8_t>(rgb));
    // The right ring: white for the player control, magenta while it chooses the wallpaper.
    const std::uint32_t right = wallpaperMode() ? WallpaperModeRgb : 0xffffff;
    m_deck->setDialRing(1, static_cast<std::uint8_t>(right >> 16), static_cast<std::uint8_t>(right >> 8),
                        static_cast<std::uint8_t>(right));
}

void PagesController::setWallpaperMode(int dial)
{
    // A press toggles: the dial that is in the mode leaves it, any other enters it.
    const int wanted = dial >= 0 && dial != m_wallpaperDial ? dial : -1;
    if (wanted == m_wallpaperDial)
        return;
    m_wallpaperDial = wanted;
    qInfo() << "galleond: wallpaper mode" << (wallpaperMode() ? "on, dial" : "off") << (wallpaperMode() ? m_wallpaperDial : -1);
    if (wallpaperMode())
        m_wallpaperTimeout.start();
    else
        m_wallpaperTimeout.stop();
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
    if (!m_launcher(app))
        qWarning() << "galleond: could not start" << app.name;
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
    m_pressed[static_cast<std::size_t>(key)] = true;
    drawKey(key, false);
    perform(action); // may change the page: the pressed flags are reset there
}

void PagesController::onKeyReleased(int key)
{
    if (m_suspended)
        return;
    if (key < 0 || key >= galleon::protocol::KeyCount || !m_pressed[static_cast<std::size_t>(key)])
        return;
    m_pressed[static_cast<std::size_t>(key)] = false;
    drawKey(key, false);
}

void PagesController::onDialRotated(int dial, int steps)
{
    if (m_suspended)
        return;
    if (dial < 0 || dial >= galleon::protocol::DialCount || steps == 0)
        return;
    if (dial == m_themeDial) {
        m_themeTimeout.start(); // in use: keep the mode
        stepTheme(steps);
        return;
    }
    if (dial == m_wallpaperDial) {
        m_wallpaperTimeout.start(); // in use: keep the mode
        Q_EMIT wallpaperStep(steps);
        return;
    }
    perform(m_config.dials[static_cast<std::size_t>(dial)].turn, steps);
}

void PagesController::onDialPressed(int dial)
{
    if (m_suspended)
        return;
    if (dial < 0 || dial >= galleon::protocol::DialCount)
        return;
    m_pressedDial = dial;
    perform(m_config.dials[static_cast<std::size_t>(dial)].press);
    m_pressedDial = -1;
}

} // namespace galleond
