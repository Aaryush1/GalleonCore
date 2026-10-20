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

- Implement `MediaController` through C++/WinRT SMTC, including session changes,
  bounded thumbnails, capability checks and safe asynchronous lifetime handling.
  The Python backend in [MusicLocal-Discord-Presence](https://github.com/Bit-Scripts/MusicLocal-Discord-Presence/blob/HEAD/backends/smtc.py)
  is a reference; no code from it has been copied in this stage.
- Replace uinput with a Windows keyboard backend and make key identifiers
  platform independent (the existing integer codes still follow Linux).
- Isolate application discovery, launch and file ownership checks.
- Replace desktop D-Bus integration and the Linux session lifecycle for Bosun.
- Validate native HID access and coexistence with other device software on real
  hardware, then add Windows deployment and packaging.

These features are not implemented by the portable-library build.
