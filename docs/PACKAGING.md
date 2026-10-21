<!--
SPDX-FileCopyrightText: 2026 Paul Woisard
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Packaging GalleonCore for Linux

Notes for whoever makes the packages (a `.deb` first: the owner uses Kubuntu 26.04 / KDE Plasma 6;
then perhaps an RPM or an Arch `PKGBUILD`). Everything below was checked against the source tree;
what is only advice is marked as such.

## What the project builds

| Piece | Files | Licence |
|---|---|---|
| `libgalleon` (protocol, `StreamDeck`, `HidTransport`, `Layout`) | `lib/` → `libgalleon.so.0.1.0` (+ headers) | LGPL-3.0-or-later |
| `galleond` (the daemon: drives the module over HID, exposes `org.galleoncore.Deck` on the session bus) | `daemon/` → `/usr/bin/galleond` | GPL-3.0-or-later |
| `galleon-bosun` (the settings window, Qt Quick + a tray icon) | `bosun/` → `/usr/bin/galleon-bosun` | GPL-3.0-or-later |
| widgets (QML shown on the module's screen) | `widgets/` → `/usr/share/galleoncore/widgets/` | GPL-3.0-or-later |
| wallpapers (24 pictures) | `public/wallpaper/` → `/usr/share/galleoncore/wallpaper/` | **CC-BY-SA-4.0** (see `ASSETS.md`, each folder has a `PROVENANCE.md`) |
| desktop file, icon | `bosun/galleon-bosun.desktop`, `bosun/icons/galleon-bosun.svg` | GPL-3.0-or-later |
| udev rules | `tools/udev/70-galleon.rules`, `71-galleon-uinput.rules` → `/usr/lib/udev/rules.d/` | GPL-3.0-or-later |
| systemd **user** service | `tools/systemd/galleond.service` → `/usr/lib/systemd/user/` | GPL-3.0-or-later |

Not to ship: `tools/check.py`, `tools/ci/`, `tools/windows/`, `tools/linux/*.c` (hardware probes),
`tools/systemd/galleond-dev.service` (runs from a build tree), `openrgb-plugin/` (separate build, needs
OpenRGB's sources), `plasmoid/` (not packaged yet), `tests/`, `fuzz/`, `docs/`, `site/`.
`public/Screensavers/` (two videos, 19 MB, CC-BY-SA-4.0) is **not installed** by `cmake --install`: the
screensaver is configured in the user's `pages.json`, which names the videos. Advice: leave them out of
the main package (an optional `galleoncore-screensavers` package could install them to
`/usr/share/galleoncore/screensavers/`, but nothing refers to that folder yet).

## Build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr \
      -DGALLEON_SOURCE_TREE_PATHS=OFF -DBUILD_TESTING=OFF
cmake --build build -j
DESTDIR=staging cmake --install build
```

- `-DGALLEON_SOURCE_TREE_PATHS=OFF` is **required for a package**: otherwise the programs carry the path of
  the build machine's source tree as a fallback. (Checked: with it OFF `strings` finds no such path.)
- `BUILD_TESTING=OFF` for the package build; the test suite is run separately (below).
- Do not pass `-DGALLEON_SANITIZE=ON` or `-DGALLEON_FUZZ=ON` (they are for development).
- C++20, CMake >= 3.21. Warnings are errors only with `-DGALLEON_WERROR=ON` (off by default).
- The install rules are in `CMakeLists.txt`, `lib/`, `daemon/`, `bosun/`. The RPATH of the build tree is dropped at install.
  `libgalleon` has SOVERSION 0.

## Dependencies

Build (names for Ubuntu 24.04 / Debian 13; Qt **6.4 or newer**):
`cmake g++ pkg-config qt6-base-dev qt6-declarative-dev libhidapi-dev` (Qt Core, Gui, Widgets, Qml, Quick, DBus,
Network; `hidapi-hidraw`).

Runtime:
- the Qt libraries above, and the QML modules `qml6-module-qtquick`, `qml6-module-qtquick-window`,
  `qml6-module-qtqml`, `qml6-module-qtqml-workerscript` (no Qt Quick Controls, no Kirigami);
- `libhidapi-hidraw0`;
- **Recommends**: `ffmpeg` (provides `ffmpeg` and `ffprobe`: video thumbnails, the animated screensaver and its conversion;
  without it those features say so and everything else works);
- **Suggests**: a music player that speaks MPRIS (VLC, Chrome/Chromium, Strawberry, ...), `wireplumber` (`wpctl`, for the mute key),
  `steam` (Steam games on a page), `openrgb` (the dials' rings through the plugin, not packaged here).
- systemd (the user service), `udev` (the rules), the kernel module `uinput` for the keypad theme (see below).

## Post-install / maintainer scripts (advice)

1. `udevadm control --reload-rules && udevadm trigger --subsystem-match=hidraw --subsystem-match=usb --attr-match=idVendor=1b1c`
   (the rules give the logged-in user access through `uaccess`: no group to join).
2. `uinput`: the "keypad" theme types with a virtual keyboard (`/dev/uinput`). The rule `71-galleon-uinput.rules`
   gives the user access; the module must be loaded. Advice: ship `/usr/lib/modules-load.d/galleoncore.conf`
   containing the single line `uinput`.
3. The service is **not** enabled by the package (it is a user service; a package cannot enable it for a user). The
   desktop file starts the settings window; the owner enables the daemon with
   `systemctl --user enable --now galleond.service`. Advice: print that line in the package's README/NEWS, and
   consider `systemd-user` preset or an XDG autostart entry for `galleon-bosun` (optional; `galleon-bosun` has a
   tray icon and hides when its window is closed).
4. Nothing is written outside the user's home at run time: configuration in `~/.config/galleoncore/`
   (`pages.json`, `galleond.conf`), data in `~/.local/share/galleoncore/` (`usage.json`, `wallpaper/mine/`,
   `screensavers/`), cache in `~/.cache/galleoncore/`.

## Where the programs look for their files (installed)

- widgets: `<XDG data dirs>/galleoncore/widgets/` (so `/usr/share/galleoncore/widgets`), also `$GALLEONCORE_WIDGETS`;
- wallpapers: the user's `~/.local/share/galleoncore/wallpaper`, then `<XDG data dirs>/galleoncore/wallpaper`
  (all of them are read and merged), also `$GALLEONCORE_WALLPAPERS`;
- the pages file: `~/.config/galleoncore/pages.json`; a built-in default is used when it does not exist (no file to ship);
- D-Bus: the session bus name `org.galleoncore.Deck`, no service file needed (the user service owns it).

## Checks before publishing a package

- `galleond --help` and `galleon-bosun --help` run; `galleon-bosun` starts under a Wayland and an X11 session.
- `reuse lint` passes on the source tree (the packaged copyright file can be written from `REUSE.toml` and the SPDX headers).
- The test suite, on the build machine: `ctest --test-dir build` (needs `dbus-run-session`; each test runs in its own
  private session bus). The project's own full check is `tools/check.py full` (Docker, Ubuntu 24.04), described in `CLAUDE.md`.
- Nothing in the package must write to the keyboard's flash or touch the RGB of the keyboard part: the project never does.

## Other formats

- Flatpak is **not** a good first target: the daemon needs `/dev/hidraw*`, `/dev/uinput`, the user's systemd and the session
  bus name, and the udev rules cannot be shipped inside a sandbox. A Flatpak of `galleon-bosun` alone would still need
  `--talk-name=org.galleoncore.Deck` and `--filesystem=xdg-config/galleoncore`, `xdg-data/galleoncore`.
- AppImage: now built by CI with separate host integration helpers for udev and the user service; see `tools/packaging/README.md`.
- Qt 6.4 is the oldest supported Qt (Ubuntu 24.04); the project's CI builds and tests on it.
- The version is `0.1.0` (`project(GalleonCore VERSION 0.1.0)` in `CMakeLists.txt`);
  the source archive is published under the `v0.1.0` tag.

## Build the Debian package

A single `galleoncore` package contains all installed components, including
libgalleon and its headers. Build on the Ubuntu 24.04 baseline (Qt 6.4.2),
not on a newer host whose library dependencies would exclude Ubuntu 24.04:

```sh
python3 tools/check.py image
mkdir -p build-deb
docker build -t galleoncore-packaging:24.04 tools/packaging
docker run --rm --user "$(id -u):$(id -g)" \
  -v "$PWD:/src:ro" -v "$PWD/build-deb:/out" \
  galleoncore-packaging:24.04 bash /src/tools/packaging/build-deb.sh
```

The script builds a Release package with source-tree paths disabled, generates
shared-library dependencies using `dpkg-shlibdeps`, and runs `ctest` in a separate
build plus `reuse lint`. Output: `build-deb/galleoncore_0.1.0-1_amd64.deb`
(on amd64; the architecture is detected). `JOBS` defaults to 4.
Build artifacts and test logs are not shipped. The package includes license texts,
a per-file SPDX inventory, wallpaper provenance, and README.Debian with the
opt-in service commands. Maintainer scripts reload udev rules and attempt to load
uinput; they never start the daemon. No graphical-session or hardware validation
is implied by container checks.

## PPA, COPR, AUR and AppImage CI

The complete recipes, CI matrix, release gates, account setup and secret names are
in [the distribution guide](../tools/packaging/README.md). Builds run on Ubuntu
24.04/26.04, Fedora 43/44/45/rawhide and Arch Linux; the AppImage uses the Ubuntu 24.04 / Qt
6.4 baseline. Publication targets are `ppa:paulwoisard/galleoncore`,
`paullux/GalleonCore` on COPR and `galleoncore` on AUR. Native source builds
run ctest before packaging; no development/test files are installed. Account
creation and publication credentials are separate from recipe validation.

The first PPA source uploads (`0.1.0-1~noble` and `0.1.0-1~resolute`) built and
passed CTest, but Launchpad rejected the binary uploads because 46 installed
files retained 1970 timestamps from the release tarball. The Debian revision 2
repair keeps that upstream tarball unchanged and stamps the staged files with
the Debian changelog date. Its manual workflow builds and checks both Ubuntu
packages before uploading `0.1.0-2~noble` and `0.1.0-2~resolute`; check the
actual Launchpad build and publication results separately from the workflow.

## First release gate

Do not create the first release tag until the Windows port is merged, the final
main commit passes all CI workflows, and the owner confirms interactive Windows
tests. Building Linux artifacts remains enabled before that confirmation;
publishing the first PPA/COPR/AUR release waits for the tag decision.

The Windows workflow also prepares a desktop ZIP as a GitHub Actions artifact
for pre-release interactive testing. It deploys Qt and hidapi and checks installed
resources with `GALLEON_SOURCE_TREE_PATHS=OFF`; it does not publish a release or
install a Windows service. Video functionality requires FFmpeg on PATH.

After the owner approves Windows testing and creates the exact version tag, the
release workflow requires the Windows desktop workflow as well as Linux checks
and packages. It attaches the tested Windows ZIP and its SHA-256 checksum to the
same GitHub release before publishing the enabled Linux repositories.

The Windows artifact also includes `GalleonCore-<version>-Windows-x86_64-Setup.exe`
and `Windows-Setup-SHA256SUMS`. NSIS receives the same staged payload as the ZIP;
CI tests installation, upgrade and uninstall with both native apps running.
The future exact-version tag uploads the tested Setup alongside the ZIP and
Linux packages. See `docs/WINDOWS.md` for lifecycle and personal-data handling.

Windows NSIS sources and generated manifests use UTF-8 with BOM. The builder
also passes `/INPUTCHARSET UTF8` so French installer strings are independent
of the build runner ANSI code page. Keep this separate from `Unicode true`,
which selects the installer runtime encoding.

Windows sign-in defaults enable galleond and Bosun for the current user through
quoted HKCU Run entries. Bosun uses `--minimized` and starts hidden when a
notification area exists. Legacy Startup shortcuts are removed. Both Run entries
are removed on uninstall and can be deselected during upgrade. Windows can still
disable a startup app through its own Startup settings; Setup does not override that choice.
Native app icons are extracted on the user's machine; they are not redistributed
inside the installer. The owner traced the ring conflict to Windows Dynamic Lighting being enabled.
No installer change claims exclusive RGB ownership. Bosun also starts a missing
daemon once when opened with its default profile; installed CI tests this path.

Windows page stability follow-up: initial launcher themes have at most 12 items;
existing explicit edits/empty slots are not auto-filled or reordered. The installed
galleond also provides `--launcher-info` for its own bounded metadata children;
that mode never opens HID. It uses the same Qt deployment as the daemon, without
an additional runtime helper binary. Test helper executables are not installed.
The application picker accepts validated Steam `.url` shortcuts, not generic URLs.
See `docs/WINDOWS.md` for current limits and hardware validation still outstanding.

The fixed-page Windows application/test sources at `5591b28` passed native build,
metadata/launcher tests and installed Setup lifecycle checks in
[run 39523931081](https://github.com/Paullux/GalleonCore/actions/runs/39523931081),
as well as full Ubuntu 24.04/Qt 6.4 validation (38 suites under both compilers,
sanitizers, fuzzing and reuse lint). Follow PR #28/main CI for the final merged
artifact. These checks do not lift the interactive hardware release gate.
