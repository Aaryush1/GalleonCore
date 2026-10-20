<!--
SPDX-FileCopyrightText: 2026 Paul Woisard
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Windows port: incremental backend separation

Linux and Windows share `main`. The temporary `codex/windows-port` branch prepares
small changes for review; it is not a second product branch.

## First stage

`libgalleon` contains the shared protocol, geometry, StreamDeck logic and HID
transport. Windows selects hidapi's native backend through its CMake package;
Linux keeps hidapi-hidraw and the existing shared library. The initial Windows
core is static, so it does not claim a public DLL ABI yet. No protocol bytes or device behaviour change.

`GALLEON_BUILD_DESKTOP` defaults to ON on Linux and OFF elsewhere. With it OFF,
only the library and its three hardware-free tests are built. Windows desktop
builds fail configuration explicitly until the remaining integrations exist.
The Windows CI builds this portable subset with MSVC and tests fake transport,
protocol and geometry. It does not test a physical keyboard or ship an installer.

The daemon creates media and keyboard services through `PlatformBackends.h`.
`LinuxPlatformBackends.cpp` supplies MPRIS and uinput. `MediaController` and
`KeyInjector` remain the interfaces used by page and screen logic. The concrete
uinput class and Linux headers live in `LinuxKeyInjector.*`.

## Windows build

Use Qt 6.4 or later, a C++20 compiler and hidapi installed through vcpkg. The CI
uses Qt 6.8.3 and MSVC 2022. Configure with the vcpkg toolchain and
`-DGALLEON_BUILD_DESKTOP=OFF -DGALLEON_SOURCE_TREE_PATHS=OFF`, build, then run
`ctest --test-dir build -C Release --output-on-failure`. Qt and hidapi
DLLs must be on PATH when running tests.

## Remaining desktop work

- Validate the C++/WinRT SMTC backend with real interactive players.
  The Python backend in [MusicLocal-Discord-Presence](https://github.com/Bit-Scripts/MusicLocal-Discord-Presence/blob/HEAD/backends/smtc.py)
  is a reference; no code from it has been copied in this stage.
- Replace uinput with a Windows keyboard backend and make key identifiers
  platform independent (the existing integer codes still follow Linux).
- Isolate application discovery, launch and file ownership checks.
- Replace desktop D-Bus integration and the Linux session lifecycle for Bosun.
- Validate native HID access and coexistence with other device software on real
  hardware, then add Windows deployment and packaging.

These features are not implemented by the portable-library build.

## SMTC backend

`media/SmtcController` implements `MediaController` over a replaceable session
interface. Its worker owns the native apartment and serializes reads and commands;
Qt only receives snapshots. The input queue holds at most 16 commands and output
coalesces into one pending snapshot. Destruction joins the worker before releasing
state. Native asynchronous operations have a 500 ms wait limit.

`WindowsSmtcSession` uses C++/WinRT and `Windows.Media.Control`. It polls the current
session once per second (also after commands), which tracks player changes and
playback/timeline updates without event subscriptions. Session generation and
identity checks reject commands for a player which has since disappeared or been
replaced. Metadata text, timelines and thumbnail allocations are bounded. Artwork
uses one private temporary file replaced atomically, with content hashes in the
URL to invalidate caches; the existing CoverLoader remains responsible for image
format and pixel validation.

Implemented commands: play/pause, next, previous, stop, relative seek, restart,
shuffle and repeat. Each native command checks the player's advertised capability.
Volume/mute require Core Audio; raising a window and fullscreen are not provided
by this backend. The Windows media factory exists, but the Linux-only desktop
build is still gated: this does not enable galleond or Bosun on Windows yet.

The design references MusicLocal-Discord-Presence's SMTC metadata flow and
[Microsoft's session API](https://learn.microsoft.com/en-us/uwp/api/windows.media.control.globalsystemmediatransportcontrolssession).
No Python, HTTP VLC fallback or external source code is incorporated. Players
which do not expose a Windows media session are not discovered.

Tests inject a fake session to cover changes/disappearance, commands, metadata
sanitization, timeline bounds, failure/recovery and worker shutdown on both Linux
and Windows. Windows also links and starts the native adapter in headless CI;
actual control of Spotify/VLC/browser sessions still needs interactive validation
on Windows, followed by testing with the physical keyboard.
