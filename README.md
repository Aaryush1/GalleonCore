<p align="center"><img src="site/logo.svg" alt="" width="96" height="96"></p>

# GalleonCore

🇫🇷 [Lire en français](README.fr.md)

🌐 [Website](https://paullux.github.io/GalleonCore/)

**The open platform for the Corsair Galleon 100 SD on Linux.**

The Galleon 100 SD is a full-size mechanical keyboard whose numpad is replaced by an Elgato Stream Deck module: 12 LCD keys, a screen and two dials. Corsair and Elgato only support the Stream Deck part on Windows and macOS. GalleonCore aims to make every part of the keyboard usable on Linux, and to integrate it with the desktop.

> ⚠️ **Early stage.** The repository holds the project structure, the build skeleton and the test tooling. Nothing talks to the hardware yet.

> GalleonCore is not affiliated with Corsair or Elgato.

## Goals

- **Stream Deck module**: draw on the 12 keys and the screen, read keys and dials.
- **Shared widgets**: the same QML widgets (clock, system monitor, now playing…) rendered on the keyboard screen *and* shown as KDE Plasma widgets on the desktop.
- **RGB lighting**: per-key RGB, mood bar and dial rings, through [OpenRGB](https://gitlab.com/CalcProgrammer1/OpenRGB).
- **OpenRGB plugin**: configure the deck from OpenRGB and sync deck themes with the lighting.
- **Windows port**, later, for everything that is not KDE-specific.

## Status

| Part | Status |
|---|---|
| Stream Deck protocol (keys, screen, dials) | Keep-alive, brightness, key and screen images implemented from the module's own descriptor and community work; screen image **confirmed on the hardware**; key images not tried yet. Input reports (keys, dials) still to do |
| Off-screen QML rendering to the deck | Wallpaper with the time (`WallpaperClock.qml`), software renderer |
| Daemon + D-Bus interface | Wallpaper picker (`galleond --preview`) and `org.galleoncore.Deck` (`SetWallpaper`, `ClearWallpaper`, `Wallpapers`) |
| KDE Plasma widgets | Skeleton |
| RGB lighting | **Under investigation.** The protocol (Corsair "V2"/Bragi) is not supported anywhere yet. Support is meant to land upstream in OpenRGB. |
| OpenRGB plugin | Planned |
| Windows | Planned, last |

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
openrgb-plugin/    (planned) OpenRGB plugin
```

Keeping the protocol free of I/O means every byte format can be unit-tested and fuzzed without the keyboard.

The RGB controller itself does not live here: it will be developed in a fork of OpenRGB and proposed as a merge request to the `CorsairPeripheralV2Controller`, so every OpenRGB user benefits from it.

## Building

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
- Everything else: **GPL-3.0-or-later**.

Full texts are in [`LICENSES/`](LICENSES/). Files carry SPDX headers, following the [REUSE](https://reuse.software) convention.
