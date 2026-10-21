<p align="center"><img src="site/logo.svg" alt="" width="96" height="96"></p>

# GalleonCore

[![Website](https://img.shields.io/badge/website-GalleonCore-46505E)](https://paullux.github.io/GalleonCore/)
[![CI](https://github.com/Paullux/GalleonCore/actions/workflows/ci.yml/badge.svg?branch=main)](https://github.com/Paullux/GalleonCore/actions/workflows/ci.yml)
[![Linux packages](https://github.com/Paullux/GalleonCore/actions/workflows/linux-packages.yml/badge.svg?branch=main)](https://github.com/Paullux/GalleonCore/actions/workflows/linux-packages.yml)
[![Windows desktop](https://github.com/Paullux/GalleonCore/actions/workflows/windows-core.yml/badge.svg?branch=main)](https://github.com/Paullux/GalleonCore/actions/workflows/windows-core.yml)
[![Release](https://img.shields.io/github/v/release/Paullux/GalleonCore)](https://github.com/Paullux/GalleonCore/releases)
[![GitHub downloads](https://img.shields.io/github/downloads/Paullux/GalleonCore/total?label=GitHub%20downloads)](https://github.com/Paullux/GalleonCore/releases)
[![Ubuntu PPA](https://img.shields.io/badge/Ubuntu-PPA-E95420?logo=ubuntu&logoColor=white)](https://launchpad.net/~paulwoisard/+archive/ubuntu/galleoncore)
[![Fedora COPR](https://img.shields.io/badge/Fedora-COPR-51A2DA?logo=fedora&logoColor=white)](https://copr.fedorainfracloud.org/coprs/paullux/GalleonCore/)
[![Arch AUR](https://img.shields.io/badge/Arch-AUR-1793D1?logo=archlinux&logoColor=white)](https://aur.archlinux.org/packages/galleoncore)
[![Linux AppImage](https://img.shields.io/badge/Linux-AppImage-8E6BBF?logo=linux&logoColor=white)](https://github.com/Paullux/GalleonCore/releases/download/v0.1.0/GalleonCore-0.1.0-x86_64.AppImage)
[![Licenses](https://img.shields.io/badge/license-GPL%20%2F%20LGPL%20%2F%20CC--BY--SA-blue)](LICENSE.md)
[![Security policy](https://img.shields.io/badge/security-report%20privately-46505E)](SECURITY.md)

🇫🇷 [Lire en français](README.fr.md)

🌐 [Website](https://paullux.github.io/GalleonCore/)

**The open platform for the Corsair Galleon 100 SD on Linux.**

The Galleon 100 SD is a full-size mechanical keyboard whose numpad is replaced by an Elgato Stream Deck module: 12 LCD keys, a screen and two dials. Corsair and Elgato only support the Stream Deck part on Windows and macOS. GalleonCore aims to make every part of the keyboard usable on Linux, and to integrate it with the desktop.

> **Active development.** The Stream Deck keys, screen and dials work on Linux, with Galleon Bosun for module settings. Plasma widgets remain planned. A Windows desktop preview with a Bosun-branded NSIS Setup and portable ZIP is being prepared for interactive testing ([details](docs/WINDOWS.md)); see the [roadmap](ROADMAP.md) for detailed validation status.

> GalleonCore is not affiliated with Corsair or Elgato.

## Goals

- **Stream Deck module**: draw on the 12 keys and the screen, read keys and dials.
- **Shared widgets**: the same QML widgets (clock, system monitor, now playing…) rendered on the keyboard screen *and* shown as KDE Plasma widgets on the desktop.
- **RGB lighting**: per-key RGB, mood bar and dial rings, through [OpenRGB](https://gitlab.com/CalcProgrammer1/OpenRGB).
- **OpenRGB plugin**: configure the deck from OpenRGB and sync deck themes with the lighting.
- **Windows desktop preview**, sharing the same core, with native media, audio and keyboard backends; hardware validation remains pending.

## Status

| Part | Status |
|---|---|
| Stream Deck protocol (keys, screen, dials) | Keep-alive, brightness, key and screen images implemented from the module's own descriptor and community work; screen image, key images, key and dial input **confirmed on the hardware** |
| Off-screen QML rendering to the deck | Wallpaper with the time (`WallpaperClock.qml`), software renderer |
| Daemon + D-Bus interface | Wallpaper picker (`galleond --preview`) and `org.galleoncore.Deck` (`SetWallpaper`, `ClearWallpaper`, `Wallpapers`) |
| Button pages (JSON) and music player (MPRIS) | Implemented, tests green; **keys, dials, the now-playing screen (real cover, title, artist, timeline) and an application key with its icon confirmed on the module** (hot reload, double press, video thumbnails and the apps window not reported yet) |
| KDE Plasma widgets | Skeleton |
| RGB lighting | **Under investigation.** The protocol (Corsair "V2"/Bragi) is not supported anywhere yet. Support is meant to land upstream in OpenRGB. |
| OpenRGB plugin | Dial rings implemented in the separate plugin; keyboard RGB remains under investigation |
| Windows | Desktop preview; native CI and interactive validation tracked in [WINDOWS.md](docs/WINDOWS.md) |

### Galleon Bosun screenshots

The five default pages below show applications installed on one Linux desktop; the apps and icons vary by computer. The last two pictures show the wallpaper and screensaver selectors. [Open the full gallery on the website](https://paullux.github.io/GalleonCore/#previews).

| Control | Multimedia |
|---|---|
| ![Bosun Control page with media keys](site/screenshots/control.png) | ![Bosun Multimedia page with browsers and media players](site/screenshots/multimedia.png) |
| Games | System |
| ![Bosun Games page with installed games](site/screenshots/games.png) | ![Bosun System page with useful tools](site/screenshots/system.png) |
| Numeric keypad | Wallpaper choice |
| ![Bosun numeric keypad](site/screenshots/numpad.png) | ![Bosun wallpaper collection](site/screenshots/wallpapers.png) |
| Screensaver choice | |
| ![Bosun screensaver selector with the two bundled videos](site/screenshots/screensavers.png) | |

### Windows: Stream Deck input does not respond

On the owner's Galleon 100 SD, images updated but the LCD keys and dials did not
respond until the module reset. Updating its Stream Deck firmware with the
official Elgato Stream Deck app on **Windows**, then uninstalling that app, restored
input in the owner's next test. Those two changes were made together, so the
test does not establish which one fixed the problem for other devices.

If you encounter the same symptom, record the firmware version first. Close
GalleonCore and other apps controlling the module. In the Elgato Stream Deck
app on Windows or macOS, open **Preferences → Devices → Galleon 100 SD →
Update Firmware**. Keep the keyboard connected until the update finishes.
Record the new version, close Stream Deck, and restart GalleonCore on Windows
for a cold-start test. If you use Stream Deck only for this keyboard, you can
uninstall it after the firmware update; leave it installed if another device
needs it. The owner's screenshot shows
version `3.06.005`, which [Elgato lists in its 7.4.1 release notes](https://edge.elgato.com/egc/sd_content/releasenotes/7.4.1.html).
The Windows test procedure and its remaining limits are in [WINDOWS.md](docs/WINDOWS.md).

The **main keyboard firmware** is separate: Corsair directs users to
[Corsair Web Hub](https://www.corsair.com/web-hub) in a compatible browser under
**Device Settings**. It requires no app installation or uninstallation. The
GIF below shows the *Stream Deck module* update in the Elgato app, not this
keyboard update.

![Elgato Stream Deck device preferences showing the Galleon firmware update control](site/firmware-update.gif)

## Architecture

```
lib/               libgalleon, C++20/Qt 6 library (LGPL-3.0-or-later)
  Protocol         pure functions: bytes ↔ events and reports, no I/O
  Transport        raw HID I/O (hidapi), replaceable by a fake in tests
  StreamDeck       keep-alive, input polling, Qt signals
daemon/            galleond: drives the deck, exposes a D-Bus API
widgets/           Shared QML widgets, free of any Plasma dependency
plasmoid/          KDE Plasma widgets reusing widgets/
tests/             Qt Test unit tests
fuzz/              libFuzzer targets for the protocol parsers
openrgb-plugin/    OpenRGB plugin for the dial rings
```

Keeping the protocol free of I/O means every byte format can be unit-tested and fuzzed without the keyboard.

The RGB controller itself does not live here: it will be developed in a fork of OpenRGB and proposed as a merge request to the `CorsairPeripheralV2Controller`, so every OpenRGB user benefits from it.

## Building

Optional: `ffmpeg` (pictures of videos) and `gio` (starting applications).

Requirements: CMake ≥ 3.21, a C++20 compiler (GCC ≥ 11 or Clang ≥ 14), Qt 6 (Core, Gui, Qml, Quick, DBus), hidapi (hidraw backend).

On Debian/Ubuntu:

```sh
sudo apt install cmake g++ qt6-base-dev qt6-declarative-dev libhidapi-dev pkg-config
cmake -B build
cmake --build build
ctest --test-dir build
```

CMake options:

| Option | Default | Effect |
|---|---|---|
| `BUILD_TESTING` | `ON` | Build the unit tests |
| `GALLEON_WERROR` | `OFF` | Treat warnings as errors |
| `GALLEON_SANITIZE` | `OFF` | AddressSanitizer + UndefinedBehaviorSanitizer |
| `GALLEON_FUZZ` | `OFF` | libFuzzer targets (Clang only) |

To let your user access the keyboard without root:

```sh
sudo cp tools/udev/70-galleon.rules /etc/udev/rules.d/
sudo udevadm control --reload && sudo udevadm trigger
```

## Code quality

Every push and pull request runs, on GitHub Actions:

- a build with strict warnings as errors, with GCC and with Clang;
- the unit tests, including under AddressSanitizer and UndefinedBehaviorSanitizer;
- 60 seconds of fuzzing on the input report parser;
- clang-tidy, qmllint, and `reuse lint` for licensing.

To reproduce the sanitizer run locally:

```sh
CXX=clang++ cmake -B build-asan -DGALLEON_SANITIZE=ON -DGALLEON_FUZZ=ON
cmake --build build-asan
ctest --test-dir build-asan
./build-asan/fuzz/fuzz_input_report -max_total_time=60
```

## Wallpaper and clock on the screen

`galleond` draws one of the images of [`public/wallpaper/`](public/wallpaper/) on the Galleon's screen (720 × 384) with the time over it, and redraws every minute.

```sh
./build/daemon/galleond --preview                         # window to pick a wallpaper
./build/daemon/galleond --wallpaper manga-cyberpunk/manga-cyberpunk-01.jpg
./build/daemon/galleond --wallpaper cinematic/cinematic-02.jpg --render-to screen.png   # no hardware
busctl --user call org.galleoncore.Deck /org/galleoncore/Deck org.galleoncore.Deck SetWallpaper s cinematic/cinematic-02.jpg
```

Only the images found in the wallpaper folder can be chosen, and each one is checked (real JPEG or PNG, size, no symbolic link). The last choice is remembered. The folder is `--wallpapers`, `$GALLEONCORE_WALLPAPERS`, `<data dir>/galleoncore/wallpaper`, or the source tree.

## Button pages

The 12 keys and the 2 dials run the actions of a JSON file, `~/.config/galleoncore/pages.json` (or `--pages file`). On a fresh Linux or Windows profile, the five built-in tabs are Contrôle, Multimédia, Jeu Vidéo, Système and Pavé Numérique. Multimedia prioritizes browsers and players, System suggests administration tools, and Video Games starts with installed Steam games. Suggestions are selected once, with at most 12 keys on each page; unrelated or unavailable launchers leave their slots empty. Removing or changing a key in Bosun does not refill or reorder it. Existing personal pages are left as they are. The file is read again when it changes; a file with an error is refused as a whole and the previous pages stay (the errors are in the log).

```json
{
  "version": 1,
  "home": "main",
  "pages": {
    "main": { "keys": [
      { "key": 0, "label": "Previous", "icon": "media-skip-backward", "action": { "type": "media", "command": "previous" } },
      { "key": 1, "label": "Play", "icon": { "playing": "media-playback-pause", "paused": "media-playback-start" },
        "action": { "type": "media", "command": "play-pause" } },
      { "key": 11, "label": "More", "action": { "type": "page", "page": "other" } }
    ] },
    "other": { "keys": [ { "key": 0, "label": "Back", "action": { "type": "back" } } ] }
  },
  "dials": { "0": { "turn": { "type": "media", "command": "volume", "step": 2 }, "press": { "type": "media", "command": "play-pause" } } }
}
```

- Keys are numbered 0 to 11, row by row from the top left. A key without an `action` does nothing.
- `icon`: a name of the desktop icon theme, or a file next to the pages file (`.png`, `.jpg`, `.svg`; no path). It can be an object with one name per player state (`playing`, `paused`, `stopped`).
- Actions: `media` (`play-pause`, `next`, `previous`, `stop`, `shuffle`, `repeat` (none, playlist, track), `mute` (the system sound, for every player), `raise` (bring the player's window to the front), `fullscreen` (a video player), `restart` (back to the start of the track), `volume` with `step` in percent, `seek` with `seconds`). A `shuffle` or `repeat` key shows its state: add `"state_labels": ["off", "on"]` (shuffle) or `["none", "list", "track"]` (repeat) to choose the words; the key is lighter while it is on, `page` (open a page), `back`, `app` (start an application, see below), `none`. A dial `turn` takes `volume` or `seek`, applied once per click.
- The music player is found through MPRIS: the one that is playing, else the last one that played.
- Actions come from this file only. The only way to start a program is an `app` key, which names a `.desktop` file: no command can be written in the file.
- `galleond --render-keys keys.png` draws the keys of the first page into an image, without the hardware.

### Applications on the keys

A key can start an application of your desktop, with no command to type: it names the application's `.desktop` file, and its name and icon are used for the key unless you give your own.

```json
{ "key": 4, "action": { "type": "app", "desktop": "firefox.desktop" } }
```

`desktop` is a desktop file ID (`firefox.desktop`, `org.kde.dolphin.desktop`) or the absolute path of a `.desktop` file. The desktop's own launcher starts it (`gio launch`, else `gtk-launch`, else `kioclient`), so `Exec`, arguments and `Terminal=true` are handled as the desktop does it. A double press starts the application once. For safety the pages file must be yours and not writable by others, or application keys are disabled.

You never need to edit the JSON for this:

```sh
./build/daemon/galleond --apps                              # a window: pick an application, pick a key
./build/daemon/galleond --list-apps --filter vlc            # the installed applications
./build/daemon/galleond --assign-app vlc.desktop --key 4    # put one on a key (--page NAME for another page)
./build/daemon/galleond --clear-key 4
```

**Galleon Bosun** (work in progress) is the settings window: one tab per theme with the 12 keys drawn as the module draws them, and tabs for the keyboard lighting, wallpapers and screensavers (not built yet). It only shows the keys for now; editing comes next.

```sh
./build/bosun/galleon-bosun
```

The edit is checked like any pages file, the previous version is kept as `pages.json.bak`, and a running `galleond` reads the change by itself.

### Themes

A `themes` list gives pages a name and a colour. A dial press with `{ "type": "theme" }` switches that dial to choosing the theme: turning it steps through the themes, and a second press goes back to the player control (after 30 seconds without a turn, it goes back by itself). The keys show the theme's page, a line in the theme's colour runs along the top of the screen with its name, and the ring around the left dial takes the colour (the right ring stays white, for the player control). While the theme is being chosen, the line along the bottom of the screen takes the theme's colour too. On a key, the same action steps to the next theme.

```json
"themes": [
  { "name": "Control", "page": "main", "color": "#ffffff" },
  { "name": "Sound", "page": "sound", "color": "#e53935" }
],
"dials": { "0": { "press": { "type": "theme" } } }
```

The ring colours use the Stream Deck protocol's per-LED feature report (`03 24 <led> <r> <g> <b>`, from rust-elgato-streamdeck PR 63). It has not been verified on the module yet. At most 8 themes; the colour is `#rrggbb`; a theme's page must exist.

### Choosing the wallpaper with a dial

The wallpapers are listed by style, then by file name with numbers read as numbers (`-2` before `-10`); the dial moves one wallpaper per turn, and turns are ignored, not queued, until the wallpaper of the last turn is really on the screen plus a rest of `screen.wallpaper_step_delay_ms` (default 150; 0: no rule, every detent counts), and the screen shows the name and place of the wallpaper being chosen (`portrait-01   10/24`).

A dial press with `{ "type": "wallpaper-mode" }` switches that dial from the player control to choosing the wallpaper: turning it steps through the wallpapers of the catalog (remembered, shown even while a track plays), and a second press validates and goes back. Turns are ignored until the screen shows the name and place of the wallpaper. After the last turn a countdown of `screen.wallpaper_validate_seconds` (default 4) is shown in a circle while the dial stays in the choice (magenta ring, a turn starts it again); when it ends the choice is validated: the dial is the player control again (white ring and line) and the chosen wallpaper stays on the screen for `screen.wallpaper_hold_seconds` seconds before a playing track takes it back. A thin line along the bottom of the screen and the right dial's ring show the mode: white for the player control, magenta for choosing the wallpaper.

### Keypad theme, automatic themes and tinted keys

A `{ "type": "key", "key": "kp7" }` action presses a keyboard key through a virtual keypad (`/dev/uinput`), held as long as the key is held. Only a fixed list of names is accepted (`kp0` to `kp9`, `kpdot`, `kpenter`, `kpplus`, `kpminus`, `kpasterisk`, `kpslash`, `numlock`, `backspace`), and the virtual device is given no other key. Num Lock must be on for the keypad digits to type digits. The virtual device needs write access to `/dev/uinput` (`tools/udev/71-galleon-uinput.rules`).

A theme can be chosen by itself with `"when": "media"` (a player starts) or `"when": "no-media"` (none is left: stopped or absent; a paused player still counts as one). Only at these moments: your own choice with the dial stays until the next change. At most one theme per trigger. The keys of a theme's page are drawn on a darker shade of the theme's colour, lighter while held.

The `screen` section also has `wallpaper_hold_seconds` (1 to 600, default 4): how long a wallpaper chosen with the right dial, or shown at start, stays on the screen before a playing track takes it back.

### Most used applications and Steam games

A page can ask for its application keys to be put in order of use with `"sort": "usage"`: the same places, the most used application in the first one, marked by a small gold star. Each launch from the module counts 1 and fades by half every 30 days, so a recent habit beats an old one. The counts are kept in `~/.local/share/galleoncore/usage.json` (at most 200 applications, written atomically).

```json
"jeux": {
  "sort": "usage",
  "steam": { "max": 6 },
  "keys": [ { "key": 0, "action": { "type": "app", "desktop": "steam.desktop" } } ]
}
```

`"menu": { "category": "Game", "max": N }` fills the free keys of the page with up to N applications of that category of the desktop menu (the freedesktop `Categories` of each `.desktop` file; `Game` for the games of the KDE menu, whatever launched them: KDE games, Lutris, Heroic, Wine, Steam shortcuts...), most used first. A menu shortcut that starts a Steam game (`steam steam://rungameid/<n>`) is scored by that game's play time and last launch from Steam's local data, even when the game is not installed here. Tools (Proton, the Steam runtimes) and anything already on the page are left out, and a Steam game shortcut and the same game from Steam's own list are shown once.

`"steam": { "max": N }` fills the free keys of the page with up to N installed Steam games, most played first. It reads Steam's own files on this machine, read-only and without network, account or key: the play time and the date of the last launch of each game (`userdata/<id>/config/localconfig.vdf`), and the name of each installed game (`appmanifest_*.acf` in the library folders). Tools such as Proton and the Steam runtimes are left out. A Steam game scores its hours played, faded by the time since its last launch, plus its launches from the module. A single game can also be a key of its own: `{ "type": "steam", "appid": 730 }` starts it through `steam steam://rungameid/730`.

### Animated screensaver

After a time without use, a vertical video can play over the screen and the 12 keys together, cut as if the picture were seen through them (the gaps between keys are left out). Any key or dial, or a track starting (unless `while_playing` is true), brings the normal screen back; the press that wakes the module does nothing else.

```json
"screensaver": {
  "video": "screensaver.mp4",
  "idle_minutes": 5,
  "fps": 15,
  "while_playing": false,
  "geometry": {
    "screen_mm": [W, H],
    "key_mm": [W, H],
    "gap_mm": [X, Y],
    "screen_to_keys_mm": D
  }
}
```

The sizes are the module's, in millimetres, measured with a ruler on the visible glass (the screen, one key, the gaps between keys, and the distance from the bottom of the screen to the top of the first row of keys). They are never guessed: without `geometry` the screensaver stays off. The module is taken as centred: the screen and the key grid share one vertical axis. The video is scaled to cover the picture (`720` pixels wide, as high as the module in proportion) and looped; make it that shape, for example `720 x 1100`, so nothing is cropped. For a seamless loop, end it on its first frame. The video path is absolute, `~/...`, or relative to the pages file. `"video"` can also be a list (up to 8), each entry a path or `{ "file": "...", "title": "Display name" }`: the videos take turns, one per start of the screensaver, and the title is written in the log. FFmpeg decodes it in its own process (no shell, lowest priority, no audio, `file` protocol only); the module takes about 20 rounds of screen plus keys per second (measured), so `fps` goes up to 20 and 15 is comfortable.

Videos you put on your own keyboard stay on your computer. A video added to this repository follows [ASSETS.md](ASSETS.md).

### Now playing on the screen

While a player is playing, the screen shows the track (cover, title, artist, time and a progress bar) instead of the wallpaper, and goes back to the wallpaper when the music stops. This is set by an optional `screen` section of the pages file:

```json
"screen": { "now_playing": "playing", "remote_covers": true, "video_thumbnails": true }
```

- `now_playing`: `playing` (default), `playing-or-paused` or `never`.
- `video_thumbnails`: `false` to never make a picture of a video with FFmpeg.
- `remote_covers`: `false` to load covers from local files only. Covers given as `https://` are downloaded by `galleond`; it never downloads from `http://`, from an IP address or from a local name, limits size, time and redirects, and only decodes JPEG, PNG and WebP.
- The cover is, in this order: the one the player gives, an image next to the playing file (`cover.jpg`, `folder.png`, `front.jpg`, `albumart.jpg`, `album.jpg`, or one named like the file), a picture of the video made with FFmpeg and kept in `~/.cache/galleoncore/thumbnails` (at most 300 pictures or 100 MB), the icon of the player (the VLC cone), then a note. A track without a title shows its file name.
- The screen follows the player at once: track and status changes arrive as D-Bus notifications, and only the progress bar is read once a second.
- `galleond --render-now-playing screen.png --wallpaper cinematic/cinematic-02.jpg` draws a sample screen without the hardware.

## Hardware notes

| Part | USB ID |
|---|---|
| Keyboard (RGB, keys) | `1b1c:2b0c` |
| Stream Deck module | `1b1c:2b18` |

Protocol notes and captures go in [`docs/protocol/`](docs/protocol/). Images and videos added to the repository follow [`ASSETS.md`](ASSETS.md).

## Credits

GalleonCore builds on the reverse engineering done by:

- [node-elgato-stream-deck](https://github.com/Julusian/node-elgato-stream-deck) (MIT), which added Galleon support in v7.5.0;
- [galleon-deck](https://github.com/NLMP-DDHS/galleon-deck) (MIT), a Python Stream Deck service for the Galleon on Linux;
- [opendeck-galleon](https://github.com/shanelord01/opendeck-galleon), an OpenDeck plugin for the Galleon.

## License

- `lib/` (libgalleon): **LGPL-3.0-or-later**, so other projects can reuse the protocol library.
- Application, widgets, tools and documentation: **GPL-3.0-or-later**.
- Repository wallpapers and videos: **CC-BY-SA-4.0**, with provenance (see [ASSETS.md](ASSETS.md)).

Full texts are in [`LICENSES/`](LICENSES/). Files carry SPDX headers, following the [REUSE](https://reuse.software) convention.

## Packages and community

Choose your operating system in the [website's downloads section](https://paullux.github.io/GalleonCore/#downloads)
for direct Windows, Ubuntu, Fedora, Arch and AppImage packages. The
[v0.1.0 release](https://github.com/Paullux/GalleonCore/releases/tag/v0.1.0)
explains how to add the PPA, COPR or AUR repository and what each file contains.
Build and packaging details are in the [distribution guide](tools/packaging/README.md).
The GitHub download counter counts release assets, not PPA/COPR/AUR installations.

[Contributing](CONTRIBUTING.md) · [Code of conduct](CODE_OF_CONDUCT.md) ·
[Security](SECURITY.md) · [Licenses](LICENSE.md).
