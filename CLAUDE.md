# GalleonCore: notes for Claude Code

Linux support for the Corsair Galleon 100 SD keyboard (built-in Elgato Stream Deck: 12 LCD keys, screen, 2 dials). Owner works on Kubuntu / KDE Plasma 6 and speaks French; answer him in French. Code, comments and public docs in English (README.fr.md is the French mirror of README.md: keep both in sync).

Project status and next steps: `ROADMAP.md` at the root (French). Read it at the start of a session and update it after every finished and tested feature (move it to "Implémenté et testé", never mark "sur le module" without the owner's feedback). Full design notes (French): `docs/ROADMAP.md`. Read them before architectural changes.

## Layout
- `lib/` libgalleon, C++20/Qt 6, **LGPL-3.0-or-later**, in three layers:
  - `Protocol.h/.cpp`: pure functions, bytes ↔ events/reports. No Qt, no hidapi, no I/O.
  - `Transport.h` (interface), `HidTransport` (hidapi). Raw I/O only.
  - `StreamDeck`: logic (keep-alive, polling, signals). Takes a `Transport`, so it is testable with `tests/FakeTransport.h`.
  - `Layout.h/.cpp`: pure geometry, splits one image over the key grid.
- `daemon/` galleond, GPL-3.0-or-later. Drives the deck; Linux exposes D-Bus (`org.galleoncore.Deck`), Windows uses per-user local IPC.
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

## Code quality (from the roadmap)
- Small functions with one job. Logic that can be pure (decisions, parsing, geometry, ordering, formatting) is kept apart from I/O (files, D-Bus, processes, the device) and takes what it needs as arguments, so it is tested without the hardware or a desktop. The layers above are the model: `Protocol` and `Layout` pure, `Transport` raw I/O, `StreamDeck` logic over a fake transport.
- Each change of behaviour comes with the test that would have caught the opposite. A test checks one behaviour, by its observable result, and is named after it.
- The settings app (`bosun/`) reads and edits the pages file and draws the keys with `galleond_core`; it never opens the device (only `galleond` holds it, for the keep-alive).

## Validation method (use `tools/check.py`)
Two kinds of runs, never confused:
- **Targeted** (`quick`, `batch`): incremental build, then only what is out of date: the tests whose program, shared library or data (the widgets) changed since they last passed in that build tree, clang-tidy on every file with the results reused for files whose content (everything they include) is the same, `qmllint`, the tests of the tool and `reuse lint` (the same replay). Changes are counted from the last tree that passed in full. Use `quick` after a small change; group small UI changes into a batch and run `batch` at its end (adds the same selection under Clang + ASan/UBSan). The report says `TARGETED CHECKS PASSED (not a full validation: N of M tests run ...)`.
- **Full** (`full`): what the CI does, on the whole project: all tests under GCC and under Clang + ASan/UBSan, fuzzing, clang-tidy on every file, `qmllint`, the tests of the tool and `reuse lint`. Those last steps are read from `.github/workflows/ci.yml` and replayed in its order in a clean copy of the tree (what would be committed), so one step cannot leave behind something the next one rejects, as happened with `__pycache__`; a test fails if the CI file renames one of them or changes the clang-tidy folders. It prints `FULL VALIDATION PASSED` and records the exact tree; `tools/check.py status` says whether that is still the tree you have. **A full validation of the current tree is required before opening or merging a pull request.**
- A targeted run is promoted to full by itself when the change touches something shared: `CMakeLists.txt`, `cmake/`, `.clang-tidy`, `.github/`, `tools/ci/`, `tools/check.py`, `REUSE.toml`, `lib/`, `fuzz/`, `data/`, `public/`, a kind of file it does not know, or when most of the tests are out of date (a shared header). If an impact is hard to bound, run `full`.
- Environment: by default in a container made from `tools/ci/Dockerfile` (Ubuntu 24.04: GCC 13, Clang 18, Qt 6.4, clang-tidy 18, like the CI; the development machine has Ubuntu 26.04 and Qt 6.10, which hides real CI failures). Build trees and caches live in docker volumes, so the next run does only what changed; the image is rebuilt only when the Dockerfile changes. `--env host` uses the tools of the machine: quicker, other versions, never enough for a merge.
- Reused results are never stale: the build tree is incremental by file dates; a test result is reused only while its program, the shared libraries and the widgets it reads are byte for byte the same as when it passed (a failed test stays out of date until it passes); a clang-tidy result only for the same tool version, the same `.clang-tidy` and the same preprocessed file; `--no-cache` ignores them.
- Limits of the targeted selection: it follows the build graph, so it does not see a test that depends on another program, the session bus, files outside the repository or an order of execution. That is what `batch` and `full` are for. Say in the report what was verified in a targeted way and what passed in full.
- The decisions of the tool are tested: `python3 -m unittest discover -s tools -p 'test_*.py'`.

## Build and check (by hand)
```sh
cmake -B build -DGALLEON_WERROR=ON && cmake --build build && ctest --test-dir build
# Sanitizers and fuzzing (Clang):
CXX=clang++ cmake -B build-asan -DGALLEON_SANITIZE=ON -DGALLEON_FUZZ=ON
cmake --build build-asan && ctest --test-dir build-asan
./build-asan/fuzz/fuzz_input_report -max_total_time=60
# Lint:
run-clang-tidy -p build '(lib/src|daemon|bosun|tests)/[^/]+\.cpp$'
/usr/lib/qt6/bin/qmllint widgets/*.qml bosun/qml/*.qml daemon/qml/*.qml
reuse lint
```

## Windows release gate
- Keep Linux and Windows backends behind shared interfaces on `main`; temporary
  port branches are merged only after full validation and green CI.
- Key action integers are internal `KeyCode` values, never OS key codes. Native
  keyboard tests must inject a fake sender and must not type real input.
- The first release tag and PPA/COPR/AUR publication wait for the owner's explicit
  confirmation of interactive Windows testing, after all port PRs are merged.
- Windows desktop integration is being prepared on `codex/windows-desktop`:
  per-user local IPC and instance locks, native launcher/file ACL checks, and
  deployed ZIP smoke tests. Keep Linux D-Bus/systemd behavior covered by tests.
  Do not confuse a successful Windows build with physical-device validation.
- Windows NSIS Setup uses the staged ZIP payload and Bosun artwork. Both upgrade
  and uninstall must request explicit per-user IPC shutdown of Bosun and galleond
  and wait for exit before changing files. Never replace this with window-close
  alone (tray mode keeps Bosun alive), or force-kill user processes on timeout.
  Keep native CI's running-app upgrade/uninstall and personal-data preservation
  tests. The Windows pipe suffix derives from the current user's SID; the
  PowerShell helper must use the same UTF-8 SHA-256/24-hex contract.

## Initial desktop content

`InitialPages` supplies five initial themes on Linux and Windows when no user
pages file exists; the bundled screensaver paths are Windows-only. Keep
`defaultPagesJson()` as the minimal portable fixture. Never overwrite customized
pages to deliver new defaults. Start Menu classification is a heuristic; Steam
discovery uses its installed library metadata. No arbitrary shell commands or URI schemes are
accepted as Windows app references.

Windows follow-up: application icons are extracted from Shell metadata and cached
as PNG; a custom label must not suppress its app icon. Keep the icon loader's file
size/dimension limits. Startup uses per-user HKCU Run commands (Bosun `--minimized`),
never a Session 0 media controller. Screensaver wake must not wait synchronously
for a decoder process; test an ignored termination request as well as consumed
wake input. Official OpenRGB ring ownership remains a hardware integration item;
do not claim priority from repeated color writes.

Fresh Linux and Windows defaults are capped at 12 launchers per theme, with no
generated Plus page, auto-refill or usage reordering on reload/edit. Existing
profiles are unchanged. First-run Multimedia suggests browsers and players,
System suggests administration tools, and Games starts with installed Steam
games; unrelated apps leave slots empty. Existing Windows page files are
normalized without Shell discovery; explicit empty slots
remain empty. Existing Linux page files retain their configured behavior. Keep
launcher/Shell/Steam artwork I/O outside the Windows device event loop:
LauncherCache runs a bounded, timed metadata-only
child (`galleond --launcher-info`), before any instance lock/HID opening. Draw one
key per event-loop turn. Long image writes refresh the existing host keep-alive
between reports without processing nested events. Cover both with slow-I/O tests;
do not claim a hardware reset fixed from synthetic tests alone.
Steam `.url` imports accept only bounded InternetShortcut files containing one
`steam://rungameid/<positive int>` URL. Ignore external icon paths and reconstruct
the validated Steam URI when launching. Other URL schemes remain rejected.
The owner identified Windows Dynamic Lighting and OpenRGB's `Dynamic Light`
LampArray device (1b1c:2b18, MI_03) as competing ring controllers. Do not invent
ownership commands or persistent RGB reports.

Windows HID startup: normal startup must open only after desktop initialization
and event-loop entry; diagnostic measurement modes can open immediately. Never
accept an initial failed keep-alive or silently ignore nonblocking setup errors.
Use explicit zero-timeout input reads. Publish disconnect before periodic reopen
so recovery redraw cannot be lost; never retry directly inside the read callback.
Manual Bosun pagination defaults to literal `NEXT >` / `< BACK`; retain user labels.

Windows HID diagnostics: the owner reproduced initial inactive controls after
PR #29, both after ten passive minutes and after manual start on a ready
session; images still work and the PID remains stable through a temporary
Corsair-logo recovery. Do not claim early startup ordering solved it or infer
handle reopening from the PID. The bounded, local Windows counter log contains
no report contents. Compare failing-first-open and recovered snapshots before
changing protocol bytes, HID ownership or retry policy. Keep the publication
gate until the owner confirms physical Windows behavior.

On 21 October, the owner reported working LCD keys and dials after updating the
Stream Deck firmware through Elgato's Windows app and then uninstalling that
app. The screenshot shows `3.06.005`. Those two interventions were not tested
separately; record the result as an observed recovery, not a proven firmware
root cause. Continue cold-start and long-idle hardware checks before the first
tag or distribution publication.
