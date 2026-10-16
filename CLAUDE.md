# GalleonCore: notes for Claude Code

Linux support for the Corsair Galleon 100 SD keyboard (built-in Elgato Stream Deck: 12 LCD keys, screen, 2 dials). Owner works on Kubuntu / KDE Plasma 6 and speaks French; answer him in French. Code, comments and public docs in English.

Full design notes (French): `docs/ROADMAP.md`. Read it before architectural changes.

## Layout
- `lib/` libgalleon, C++17/Qt 6, **LGPL-3.0-or-later**. Stream Deck HID protocol (hidapi) and off-screen QML rendering to JPEG.
- `daemon/` galleond, GPL-3.0-or-later. Drives the deck, exposes D-Bus (`org.galleoncore.Deck`).
- `widgets/` shared pure-QML widgets. **No PlasmaCore/Kirigami imports here**: they must render off-screen in galleond.
- `plasmoid/` KDE Plasma 6 widgets reusing `widgets/`.
- `docs/protocol/` protocol notes and USB captures.
- RGB controller is NOT developed here: it goes to a fork of OpenRGB (`Controllers/CorsairPeripheralV2Controller/`).

## Rules
- Every source file starts with SPDX headers (`SPDX-FileCopyrightText: 2026 Paul Woisard`, `SPDX-License-Identifier`). `lib/` uses LGPL-3.0-or-later, the rest GPL-3.0-or-later.
- Never invent protocol bytes. Protocol facts come from node-elgato-stream-deck, galleon-deck (`docs/PROTOCOL.md`) or real captures in `docs/protocol/`; cite the source in a comment.
- RGB writes can freeze the keyboard until unplugged. Never send anything that writes to flash (hardware save, persistent mode).

## Hardware facts
- Keyboard part `1b1c:2b0c`, Stream Deck part `1b1c:2b18`.
- Deck enters host mode only while a keep-alive feature report is sent every 500 ms; otherwise it falls back to numpad mode.
- Images are JPEG. Top screen resolution unconfirmed (1280×720 vs 720×180).

## Build
```sh
cmake -B build && cmake --build build
```
