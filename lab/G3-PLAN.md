# G3: make it solid (safety pieces built offline 2026-10-25; desk checks pending)

## Built (no device writes; offline self-test 59/59)

- **`tools/GalleonLink`** (library): HID transport, Bragi v1.5 framing, allowlist, frame builders, and:
  - `Keyboard`, the only way the helper drives the keys:
    - on connect, **releases first** if an earlier run left the keyboard in host mode (`Recovery.NeedsReleaseOnStart`);
    - **20 fps cap** (`RateLimiter`); frames that arrive too fast are dropped, never queued;
    - **freeze stop** (`FreezePolicy`): two failed commands in a row (timeout or non-zero status) → release and refuse further frames;
    - **releases on Windows suspend and sign-out/shutdown** (`SystemEvents`), and on `Dispose`.
  - `Frames.FromKeys(background, {usage → color})`: per-key frames by USB HID usage, LED index < 146 enforced.
- **`tools/Guard`** (watchdog): `Guard <pid>` waits for that process to exit, then connects with a fresh session. Connecting releases if the keyboard is still in host mode. Retries 3×, logs the time from exit to release.
- **`tools/Probe` G3 steps:** `g3-crash`, `g3-sweep`, `g3-endurance`, all going through `Keyboard`.

## Desk checks (≈ 20 min, he watches; no camera)

| Check | Command | He does | Pass |
|---|---|---|---|
| D4 with watchdog | `Probe g3-crash` | watch: blue for 5 s, then the tool crashes on purpose (FailFast, no release) | keys back to his green + red typing **within 2 s** of the crash, no replug; guard log shows exit-to-release time |
| Index sweep | `Probe g3-sweep` | for each of 3 groups (6 s each), say what lights magenta (the 18 indices his layers don't use as keys) | a map of what those LEDs are (top strip? extra keys? nothing) |
| D2 + D3 endurance | `Probe g3-endurance` | glance at start and end; type continuously for 5 min somewhere inside the 10 | 20 fps for 10 min, zero freeze trips, no typing lag or missed keys, back to his profile at the end |
| D5 everyday events | `Probe g3-endurance` running, then: sleep + wake; unplug + replug keyboard; open Web Hub | confirm each recovers | keys back to his profile on sleep; after wake/replug the next run reconnects cleanly; Web Hub conflict behavior recorded |

## Known limitations

- **No camera.** Long runs are verified from device replies and logs, plus his glances. A run where the device acks but shows wrong colors can go unnoticed between glances.
- **Guard needs to be running.** If both the helper and Guard die (e.g. PC power loss), the keyboard keeps the last frame until the next start releases it. Typing is unaffected.
- **USB replug while in host mode:** expected to come back in self-operated mode (power cycle), not yet verified.
