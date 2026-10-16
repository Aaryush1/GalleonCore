# GalleonCore: notes for Claude Code

Linux support for the Corsair Galleon 100 SD keyboard (built-in Elgato Stream Deck: 12 LCD keys, screen, 2 dials). Owner works on Kubuntu / KDE Plasma 6 and speaks French; answer him in French. Code, comments and public docs in English (README.fr.md is the French mirror of README.md: keep both in sync).

Full design notes (French): `docs/ROADMAP.md`. Read it before architectural changes.

## Layout
- `lib/` libgalleon, C++20/Qt 6, **LGPL-3.0-or-later**, in three layers:
  - `Protocol.h/.cpp`: pure functions, bytes ↔ events/reports. No Qt, no hidapi, no I/O.
  - `Transport.h` (interface), `HidTransport` (hidapi). Raw I/O only.
  - `StreamDeck`: logic (keep-alive, polling, signals). Takes a `Transport`, so it is testable with `tests/FakeTransport.h`.
  - `Layout.h/.cpp`: pure geometry, splits one image over the key grid.
- `daemon/` galleond, GPL-3.0-or-later. Drives the deck, exposes D-Bus (`org.galleoncore.Deck`).
- `widgets/` shared pure-QML widgets. **No PlasmaCore/Kirigami imports here**: they must render off-screen in galleond.
- `plasmoid/` KDE Plasma 6 widgets reusing `widgets/`.
- `tests/` Qt Test, `fuzz/` libFuzzer targets for the protocol layer.
- `docs/protocol/` protocol notes and USB captures.
- `data/devices/` device descriptions (screen size, key size and gaps in mm, measured throughput). `null` until measured: never fill a value without a measurement or a cited source.
- RGB controller is NOT developed here: it goes to a fork of OpenRGB (`Controllers/CorsairPeripheralV2Controller/`).

## Rules
- **All external data is untrusted until fully validated**: HID reports from the device, image and video files imported by the user, configuration files. Validate the length, then the values (report ID, key index 0..11, plausible dial steps), then the sequence (no release without a press, no unexpected report in the middle of a transfer). A device claiming `1b1c:2b18` is not proof the reports are well formed. Invalid input is dropped and logged, never trusted, never a crash.
- Every source file starts with SPDX headers (`SPDX-FileCopyrightText: 2026 Paul Woisard`, `SPDX-License-Identifier`). `lib/` uses LGPL-3.0-or-later, the rest GPL-3.0-or-later. `reuse lint` must pass.
- Never invent protocol bytes. Protocol facts come from node-elgato-stream-deck, galleon-deck (`docs/PROTOCOL.md`) or real captures in `docs/protocol/`; cite the source in a comment.
- RGB writes can freeze the keyboard until unplugged. Never send anything that writes to flash (hardware save, persistent mode).
- Memory safety: no raw `new`/`delete` (use `std::unique_ptr`, or Qt parent ownership for QObjects), no pointer arithmetic on HID buffers (use `std::span`/`std::array`), check every length before reading a report.
- Images and videos in the repository follow `ASSETS.md`: no existing characters, trademarks, real people or footage; CC-BY-SA-4.0 by default; provenance recorded.
- Every protocol change comes with a Qt Test case, ideally built from a real capture. Parsers must stay fuzzable (`fuzz/`).
- Code must build with `-DGALLEON_WERROR=ON` and pass clang-tidy (`.clang-tidy`) and the tests under `-DGALLEON_SANITIZE=ON` (ASan, UBSan and LeakSanitizer, enabled explicitly).
- LeakSanitizer only sees memory unreachable at exit. Long-lived state in galleond (frame caches, QObject children) must be bounded, so it cannot grow without limit while still reachable.

## Hardware facts
- Keyboard part `1b1c:2b0c`, Stream Deck part `1b1c:2b18`.
- Deck enters host mode only while a keep-alive feature report is sent every 500 ms; otherwise it falls back to numpad mode.
- Images are JPEG. Top screen resolution unconfirmed (1280×720 vs 720×180).

## Build and check
```sh
cmake -B build -DGALLEON_WERROR=ON && cmake --build build && ctest --test-dir build
# Sanitizers and fuzzing (Clang):
CXX=clang++ cmake -B build-asan -DGALLEON_SANITIZE=ON -DGALLEON_FUZZ=ON
cmake --build build-asan && ctest --test-dir build-asan
./build-asan/fuzz/fuzz_input_report -max_total_time=60
# Lint:
run-clang-tidy -p build '(lib/src|daemon|tests)/[^/]+\.cpp$'
/usr/lib/qt6/bin/qmllint widgets/*.qml
reuse lint
```
