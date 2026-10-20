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
the portable library and backend tests are built. Windows desktop
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
- Complete native Windows CI and interactive validation of the new keyboard
  backend; key identifiers are now platform independent.
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
Volume/mute use Core Audio on the default multimedia output; raising a window and fullscreen are not provided
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

## Core Audio volume and mute

The Windows worker handles volume and mute before checking for an SMTC player.
`IMMDeviceEnumerator::GetDefaultAudioEndpoint(eRender, eMultimedia)` resolves the
current default output on each command. `IAudioEndpointVolume` reads its current
scalar level or mute state before applying the requested change. Volume deltas
are percentage points, clamped to 0–100%, and preserve mute. Toggling mute leaves
the volume unchanged. A zero delta performs no operation.

COM interfaces are scoped to the command on the initialized MTA worker; no device
is cached across commands. A removed output is not retried automatically (which
could toggle mute twice), and the next command can resolve a replacement device.
Command errors do not clear SMTC metadata or discard the rest of the queue.

Tests use fake endpoints for numeric bounds, mute preservation and read/write
failures, and a fake session for audio commands without a player and error
isolation. CI does not modify the runner's real volume or mute state. Native
Windows compilation/linking is checked; audible effects and default-output
switches still require interactive Windows validation.

API reference: [Microsoft Core Audio endpoint volume](https://learn.microsoft.com/en-us/windows/win32/api/endpointvolume/nn-endpointvolume-iaudioendpointvolume).

## Keyboard backend in preparation

The shared whitelist now uses internal key identifiers rather than Linux event
codes. Linux translates them to the same uinput keys. Windows translates them
to scan codes through MapVirtualKeyW and sends press/release events with
SendInput. Held keys are released on orderly destruction, duplicate presses are
ignored, and failed releases remain eligible for cleanup. Tests inject a fake
sender; no actual keyboard input is generated by CI.

This backend does not yet enable the Windows desktop executables. Application
launching, local IPC, lifecycle and deployment remain prerequisites. Interactive
Windows testing is still required before the first release tag.

References: [MapVirtualKeyW](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-mapvirtualkeyw)
and [SendInput](https://learn.microsoft.com/en-us/windows/win32/api/winuser/nf-winuser-sendinput).
