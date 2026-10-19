// SPDX-FileCopyrightText: 2026 Paul Woisard
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include "DesktopEntries.h"
#include "KeyInjector.h"
#include "MediaController.h"
#include "PagesConfig.h"
#include "SteamLibrary.h"
#include "TileRenderer.h"
#include "UsageStats.h"

#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QStringList>
#include <QTimer>

#include <array>
#include <functional>
#include <memory>
#include <optional>

namespace galleon {
class StreamDeck;
}

namespace galleond {

// Puts the button pages on the 12 keys and acts on the keys and dials.
//
// It draws only the keys whose picture changed, so a state change of the
// player redraws one key, not twelve. A key held down is drawn lighter. The
// actions come only from the pages file: nothing here runs a program.
class PagesController : public QObject
{
    Q_OBJECT

public:
    // `deck` and `media` are not owned and must outlive the controller.
    PagesController(galleon::StreamDeck *deck, MediaController *media, TileRenderer tiles, QObject *parent = nullptr);

    // Uses this configuration and goes back to its home page.
    void setConfig(const PagesConfig &config);

    // Reads, validates and applies a pages file, then watches it: a change on
    // disk is read again. A file with errors is refused as a whole, its errors
    // are logged, and the configuration in use stays. A missing file is not an
    // error: the default pages are used until it appears.
    void loadFile(const QString &path);

    // How an application is started; the tests replace it. The default asks the
    // desktop's launcher (gio launch) to start the .desktop file.
    using AppLauncher = std::function<bool(const DesktopApp &)>;
    void setAppLauncher(AppLauncher launcher) { m_launcher = std::move(launcher); }

    // Where `key` actions go (not owned, must outlive the controller). Without
    // one, key actions do nothing.
    void setKeyInjector(KeyInjector *injector) { m_injector = injector; }

    // Where the use of applications is kept (default: nowhere, in memory only), how a Steam
    // game is started, and where the installed Steam games come from; the tests replace them.
    void setUsageFile(const QString &file);
    using SteamLauncher = std::function<bool(int appid)>;
    void setSteamLauncher(SteamLauncher launcher) { m_steamLauncher = std::move(launcher); }
    using SteamGames = std::function<QList<SteamGame>()>;
    void setSteamGames(SteamGames provider)
    {
        m_steamGames = std::move(provider);
        m_steamCacheValid = false;
        rebuildPage();
    }

    [[nodiscard]] const ScreenConfig &screenConfig() const { return m_config.screen; }
    [[nodiscard]] const ScreensaverConfig &screensaverConfig() const { return m_config.screensaver; }
    [[nodiscard]] QString currentPage() const { return m_stack.isEmpty() ? QString() : m_stack.last(); }
    [[nodiscard]] QStringList pageStack() const { return m_stack; }

    // Draws the keys; `force` sends every picture even if it did not change
    // (a module that was plugged in again shows its old icons).
    void redraw(bool force = false);

    // The theme shown now (an entry of `themes`), or nullptr when the file has
    // none or the current page belongs to no theme. Valid until the config changes.
    [[nodiscard]] const Theme *currentTheme() const;

    // While suspended (the screensaver runs) the keys are not drawn, the rings
    // are dark, and keys and dials do nothing. Leaving it redraws everything.
    void setSuspended(bool suspended);
    [[nodiscard]] bool isSuspended() const { return m_suspended; }

    // True while a dial is set to choosing the theme (its press toggles it;
    // turning it steps through the themes).
    [[nodiscard]] bool themeMode() const { return m_themeDial >= 0; }

    // True while a dial is set to choosing the wallpaper (its press toggles it).
    [[nodiscard]] bool wallpaperMode() const { return m_wallpaperDial >= 0; }

    // The screen says the name and place of the wallpaper are shown: from now on a turn of
    // the dial chooses (before, it is ignored, so that a turn is never made blind).
    void wallpaperLabelShown();

    // Lights the left dial ring with the current theme's colour (off without
    // a theme) and the right one white. Also done by every theme change; call it again
    // when the module was plugged in again.
    void applyRings();

Q_SIGNALS:
    // The validation countdown of the wallpaper choice: seconds left (N .. 1), then 0 when it
    // is over or the mode is left. The screen shows it.
    void wallpaperCountdown(int secondsLeft);
    // The dial in wallpaper mode turned: `steps` wallpapers forward (negative: back).
    void wallpaperStep(int steps);
    // Theme mode was entered or left.
    void themeModeChanged();
    // Wallpaper mode was entered or left.
    void wallpaperModeChanged();
    // The theme changed (also to none): the screen's line and the rings follow.
    void themeChanged();
    // The `screen` section of the configuration in use changed.
    void screenConfigChanged();
    // The `screensaver` section changed.
    void screensaverConfigChanged();

private:
    // The configuration of the key on the current page, or nullptr. Points
    // into m_config: valid until the configuration or the page changes.
    [[nodiscard]] const KeyConfig *keyConfig(int key) const;
    void onKeyPressed(int key);
    void onKeyReleased(int key);
    void onDialRotated(int dial, int steps);
    void onDialPressed(int dial);
    void perform(const Action &action, int times = 1);
    void launchApp(const QString &desktop);
    // The application behind a reference of the pages file (looked up once).
    [[nodiscard]] DesktopApp appFor(const QString &desktop);
    void drawKey(int key, bool force);
    void reload();
    void watch(const QString &path);

    galleon::StreamDeck *m_deck;
    MediaController *m_media;
    TileRenderer m_tiles;
    PagesConfig m_config;
    void selectTheme(int index);
    // The current page as shown: the pages file's, with its application keys put in order of use and
    // the installed Steam games added, when the page asks for it.
    void rebuildPage();
    [[nodiscard]] const Page *effectivePage() const;
    void launchSteam(int appid);
    void recordUse(const QString &id);
    void onMediaChanged();
    void releaseHeldKeys();
    [[nodiscard]] static bool hasKeyActions(const PagesConfig &config);
    [[nodiscard]] int themeFor(ThemeTrigger trigger) const;
    void stepTheme(int steps);

    void setWallpaperMode(int dial); // -1 leaves the mode
    void setThemeMode(int dial);     // -1 leaves the mode

    // Clicking a dial moves it a little: a step just before a press, or just after
    // it, is the click and not a turn. Each dial remembers its latest steps and press.
    struct RecentTurn
    {
        qint64 at = 0; // ms on m_clock
        int steps = 0;
    };
    // A press moves the dial before the contact closes (pushing) and after it opens
    // (letting go): steps up to this long before a press, or after a press or a release, are the click's.
    static constexpr int ClickBeforeMs = 350;
    static constexpr int ClickAfterMs = 250;
    qint64 m_lastWallpaperStepAt = -100000; // when the last wallpaper step was taken
    QElapsedTimer m_clock;
    std::array<std::array<RecentTurn, 6>, galleon::protocol::DialCount> m_recentTurns{};
    std::array<qint64, galleon::protocol::DialCount> m_lastPressAt{};   // a press or a release
    void onDialReleased(int dial);
    std::unique_ptr<UsageStats> m_usage = std::make_unique<UsageStats>();
    QList<SteamGame> m_steamCache;
    bool m_steamCacheValid = false;
    QElapsedTimer m_steamCacheAge;
    std::optional<Page> m_dynamic; // the page as shown, when it differs from the file's
    QString m_dynamicFor;
    int m_highlightKey = -1;
    KeyInjector *m_injector = nullptr;
    std::array<int, galleon::protocol::KeyCount> m_heldKey{}; // key code held for each key, 0 = none
    bool m_mediaPresent = false;
    int m_failedSends = 0;
    bool m_suspended = false;
    int m_themeDial = -1; // the dial that chooses the theme, -1 = none
    QTimer m_themeTimeout;
    QTimer m_themeApply; // groups the theme steps of a turning dial into one redraw
    void applyTheme();
    bool m_themePending = false;

    int m_pressedDial = -1; // the dial whose press is being performed
    int m_wallpaperDial = -1; // the dial that chooses the wallpaper, -1 = none
    QTimer m_wallpaperTimeout; // ticks each second of the validation countdown
    int m_wallpaperLeft = 0;   // seconds of the countdown left
    bool m_wallpaperBusy = false;  // a step was taken and its picture is not on the screen yet
    bool m_wallpaperReady = false; // the name and place of the wallpaper are on the screen: turns count
    QTimer m_wallpaperReadyFallback;
    void restartWallpaperCountdown();
    int m_theme = -1; // index in m_config.themes, -1 = none
    QStringList m_stack; // opened pages, the current one last
    std::array<bool, galleon::protocol::KeyCount> m_pressed{};
    std::array<QImage, galleon::protocol::KeyCount> m_sent; // what the keys show now

    AppLauncher m_launcher;
    SteamLauncher m_steamLauncher;
    SteamGames m_steamGames;
    QHash<QString, DesktopApp> m_apps; // looked-up references, a handful: cleared on every reload
    QHash<QString, bool> m_warned;     // what was already said once in the log
    bool m_appsAllowed = true;
    QElapsedTimer m_lastLaunch;

    QString m_file;
    QFileSystemWatcher m_watcher;
    QTimer m_reloadTimer;
};

} // namespace galleond
