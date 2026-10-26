Title: Live per-key lighting works on firmware 2.3.88 (host-controlled mode, resource 0x22)

Hi Paul,

Thank you for GalleonCore. Your protocol notes, the bragi-probe allowlist and the Web Hub analysis gave us a working starting point, so we didn't have to reverse-engineer the Galleon 100 SD from scratch. `docs/protocol/README.md` says no capture of a live per-key stream exists yet. We have one working on hardware and wanted to share it before proposing any change, as CONTRIBUTING asks for protocol changes.

## Setup

Galleon 100 SD (1b1c:2b0c), firmware 2.3.88, Windows 11, 128-byte reports on MI_01 (usage page 0xFF42). The test tools are a small C# library in our fork, under `lab/` on the `key-lighting` branch: https://github.com/Aaryush1/GalleonCore/tree/key-lighting/lab. Every command goes through an allowlist modeled on bragi-probe. The tools only ever send property 3 = 1 or 2, and they never create, write or delete files and never factory-reset.

## The sequence

1. Start a session (`1b`, `[1, rnd4, 0]`).
2. Set property 3 to 2 (host-controlled).
3. Open buffer 0 on resource `0x22` (`0d 00 22 00`).
4. Write one frame with op `06` then op `07` continuations. The frame is `12 00` followed by 146 interleaved RGB triplets (440 bytes).
5. To hand control back, unbind buffer 0 (`05 01 00`) and set property 3 to 1.

The LED index is the USB HID usage code. `0x3A` is F1, which the owner confirmed by eye. `0x81` (129) is the Copilot key, matching Web Hub's key table. `0x82` and above drive the light strip along the top edge. The first few strip indices map to single LEDs from the left, and later indices light sections of the strip. We have not finished that map.

## Behavior worth documenting

- **The keyboard never releases host mode by itself.** After the host process crashes, it keeps showing the last frame in mode 2. Typing still works. A new session can release it (unbind, then mode 1) without a replug, and a USB replug also returns it to mode 1. We run a watchdog process that does this; it released the keys 0.38 s after a deliberate crash.
- **Web Hub takes over silently.** When Web Hub connects, it sets mode 1, but our writes keep returning status 0, so the host gets no error. Web Hub's polling replies arrive on the shared MI_01 input about every 2.5 s. Our tool treats any reply it didn't ask for, or a mode other than 2, as another host present, and stops sending. That ended the flicker we saw at first.
- **Sleep:** releasing on Windows suspend works, and the keyboard wakes into its onboard profile.

## Measurements

The owner watched every visual result. Counts come from our command log.

- 20 frames/s for 10 minutes: 0 failed writes, 0 timeouts. The owner typed fast during the run and saw no lag and no dropped keys.
- One frame takes about 28 ms to write (four reports, each waiting for its reply).
- Drawing a typing glow from Raw Input took a median of 31 ms from key press to light (p95 67 ms).

## Offer

If it's useful, I can open a docs-only PR that adds this sequence and the release behavior to `docs/protocol/README.md`, with the evidence above. Our code is C#, so it doesn't fit the C++ codebase as is, but it's GPL-3.0 and available as a reference. I can share sanitized log extracts (command bytes only, no keystrokes) if you want to check anything.

Thanks again for the groundwork.
