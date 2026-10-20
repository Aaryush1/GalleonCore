<!--
SPDX-FileCopyrightText: 2026 Paul Woisard
SPDX-License-Identifier: GPL-3.0-or-later
-->

# Windows port: incremental backend separation

Linux and Windows share `main`. The temporary `codex/windows-desktop` branch prepares
small changes for review; it is not a second product branch.

## First stage

`libgalleon` contains the shared protocol, geometry, StreamDeck logic and HID
transport. Windows selects hidapi's native backend through its CMake package;
Linux keeps hidapi-hidraw and the existing shared library. The initial Windows
core is static, so it does not claim a public DLL ABI yet. No protocol bytes or device behaviour change.

`GALLEON_BUILD_DESKTOP` defaults to ON on Linux and OFF elsewhere. With it OFF,
the portable library and backend tests are built. Windows desktop builds are explicit opt-in while native integration is being validated.
The Windows CI builds this portable subset with MSVC and tests fake transport,
protocol and geometry. It does not test a physical keyboard or ship an installer.

The daemon creates media and keyboard services through `PlatformBackends.h`.
`LinuxPlatformBackends.cpp` supplies MPRIS and uinput. `MediaController` and
`KeyInjector` remain the interfaces used by page and screen logic. The concrete
uinput class and Linux headers live in `LinuxKeyInjector.*`.

## Windows build

Use Qt 6.4 or later, a C++20 compiler and hidapi installed through vcpkg. The CI
uses Qt 6.8.3 and MSVC 2022. Configure with the vcpkg toolchain and
`-DGALLEON_BUILD_DESKTOP=ON -DGALLEON_SOURCE_TREE_PATHS=OFF -DGALLEON_WERROR=ON`, build, then run
`ctest --test-dir build -C Release --output-on-failure`. Qt and hidapi
DLLs must be on PATH when running tests. Use `GALLEON_BUILD_DESKTOP=OFF` to build only the portable subset. Windows 10 (1809+) or Windows 11 x64 is the target; the MSVC x64 runtime must be installed.

## Remaining desktop work

- Validate the C++/WinRT SMTC backend with real interactive players.
  The Python backend in [MusicLocal-Discord-Presence](https://github.com/Bit-Scripts/MusicLocal-Discord-Presence/blob/HEAD/backends/smtc.py)
  is a reference; no code from it has been copied in this stage.
- Complete native Windows CI and interactive validation of the new keyboard
  backend; key identifiers are now platform independent.
- Validate Windows application discovery, launching and file ACL checks.
- Validate the local Windows IPC and executable lifecycle for Bosun.
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
by this backend. The Windows media factory is selected by the Windows desktop build;
interactive validation with galleond and Bosun is still required.

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

## Desktop integration in preparation

`-DGALLEON_BUILD_DESKTOP=ON` now selects native Windows integrations for both
executables. Linux keeps MPRIS, uinput, D-Bus and its systemd user service.
Windows uses a per-user instance lock before HID access, a local Qt named pipe
restricted to the creating user, and a bounded JSON protocol for status,
wallpaper application and shutdown. Bosun starts the sibling `galleond.exe`;
it does not open the keyboard itself or install a Windows service.

The shared pages file lives under `GenericConfigLocation/galleoncore`, so both
executables use the same path on Windows. Before application actions are
allowed, the open configuration file's owner and ACL are checked. Write access
is accepted only for the current user, SYSTEM and local administrators; unknown
ACL forms fail closed. Start Menu shortcuts and absolute `.exe` references replace
Linux `.desktop` references. Linux configuration files keep their original rules.

Installed Windows resources are found relative to `bin/../share/galleoncore`.
The native CI builds both executables, runs portable backend tests and renderer
tests without HID, deploys Qt/hidapi, and produces an archive with preview images
for interactive testing. FFmpeg is an external prerequisite for video features;
install an x64 FFmpeg build and make `ffmpeg.exe` and `ffprobe.exe` available on PATH.
No video tool is silently downloaded by the application.

Before calling the port ready, validate the installed archive on Windows, then
check on real hardware: HID reconnect, media metadata and commands, output volume
and mute, Num Lock on/off and key releases, start/stop from Bosun, application
launches, personal wallpaper imports and animated screensavers. Windows startup
registration is manual in this stage. A first release tag still requires the
owner's explicit confirmation after those interactive tests.

## NSIS Setup

The Windows artifact contains both the portable ZIP and a versioned NSIS Setup,
with SHA-256 checksums. Setup uses Bosun's own icon and charcoal/amber graphics,
includes English and French UI resources, and installs for the current user under
`LocalAppData/Programs/GalleonCore`. Shortcuts and starting galleond at sign-in
are selectable; no system-wide Windows service is installed.

Before an upgrade or uninstall, Setup sends an explicit shutdown request to both
Bosun and galleond through SID-derived per-user pipes. It waits for the installed
processes to exit before touching their files. Closing Bosun's window is not used
as a substitute: the tray mode can keep it running. If shutdown fails, Setup
aborts rather than replacing locked binaries. It does not forcibly terminate a
user process. A validated manifest removes only the previous installed payload
on upgrade; personal files and settings are outside the installation and remain.

Native CI installs silently, starts both applications, upgrades while they are
running, starts them again and uninstalls. It checks orderly exits, removal of
application binaries, and preservation of a personal-file and registry-setting
sentinel. This runs only on isolated GitHub runners, without a physical Galleon.
A successful setup test does not replace the owner's hardware validation.

## Initial pages and bundled screensavers

A fresh Windows configuration includes Contrôle, Multimédia, Jeu Vidéo, Système
and Pavé Numérique. Multimedia discovers common browsers and players from Start
Menu shortcuts. Games include installed Steam titles and shortcuts in Games/Jeux
folders; this does not enumerate every third-party store. System keys target
Windows utilities actually present in System32. The two videos in
`public/Screensavers` are installed under `share/galleoncore/screensavers` and
selected by default with the owner's Linux geometry. FFmpeg remains required for
video decoding, probing and conversion, as described above.

Existing customized pages are preserved. Initial contents apply only when there
is no pages.json; copying the user's Linux configuration is not required.

## Follow-up: native icons, waking, and sign-in

The shell integration extracts application icons into a per-user PNG cache,
resolves local shortcut targets without executing them, omits uninstall/help/WSL
shortcuts, and includes common/user Start Menus, Desktop shortcuts and App Paths
registrations. Browse also accepts an explicit executable or shortcut. It does
not claim to enumerate every packaged Store app. User labels no longer suppress
the application's icon. Steam artwork also checks older flat cache layouts.

Setup enables sign-in shortcuts for galleond and Bosun by default. Bosun's sign-in
shortcut passes `--minimized`; with a notification area it hides its window and
keeps the tray icon. Without a tray the window remains accessible. Clearing the
component during an upgrade removes previous sign-in shortcuts. Uninstall removes
both. These are interactive user-session processes, not Session 0 services.

Waking a screensaver now releases the display without synchronously waiting for
FFmpeg to terminate. An unresponsive decoder is killed asynchronously after its
grace period. The wake gesture is still consumed. Interactive immediate and long
idle wake tests remain required.

With official OpenRGB the owner reports the dial LampArray as `Keyboard` and the
keyboard RGB separately as `Corsair Galleon 100 SD`. Ring ownership must be given
to galleond; repeatedly fighting another RGB writer does not establish priority.
Diagnose by exiting OpenRGB, changing the Galleon theme and checking ring colors,
then checking the relevant detector/device exclusions in that OpenRGB version.
No global RGB configuration is changed by GalleonCore.
