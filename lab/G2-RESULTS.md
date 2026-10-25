# G2 results: live per-key lighting works on firmware 2.3.88 (2026-10-25, 17:11–17:15 CDT)

Aaryush at the desk, watching and typing at each step. Tool: `tools/Probe` with `--g2` allowlist (property 3 = 1/2 only; writes only to buffer 0 bound to resource 0x22, ≤ 440 bytes; unconditional release). Logs: `logs/g2-*.log` (gitignored). **No stored file was written. Every step released cleanly to mode 1 (confirmed by reading property 3).**

| Step | Sent | Device replies | What he saw | Typing |
|---|---|---|---|---|
| 2.0 | session, get 3, firmware, ping | all status 0; mode 1; fw 2.3.88 | n/a | n/a |
| 2.1 | set 3 = 2, hold 10 s, release | all status 0 | **keys unchanged (his green)** | fine throughout |
| 2.2 | mode 2, open 0x22, one frame `12 00` + 146 × RGB(0,0,255) (440 B, op 6 + 3 × op 7), hold 10 s, release | all status 0 | **all keys blue**, then back to his green + red typing layer on release | fine |
| 2.3 / 2.4 | skipped: 2.2 worked | n/a | n/a | n/a |
| 2.5 | same frame streamed 10 fps × 10 s (100 frames) | 100 × status 0 | **solid blue, no flicker**, back to green after | **no lag, no missed keys** while typing continuously |
| 2.6 | one frame, only LED index 0x3A green, rest black | all status 0 | **only F1 green** | fine |

## What this establishes

1. **A volatile live path exists on firmware 2.3.88.** It's the first confirmed one for the Galleon keys on 2.x. GalleonCore's notes say no host lighting worked on 2.3.88; their tests used LampArray and never wrote a Bragi color buffer.
2. The recipe:
   1. Session (op 0x1B).
   2. Set property 3 = 2.
   3. Open buffer 0 on resource **0x22** (op 0x0D `[0, 22 00]`).
   4. Write frame `12 00` + interleaved RGB per LED with op 6 `[0, len LE32, data(119)]` then op 7 `[0, data(123)]` continuations, in 128-byte reports.
   5. Unbind buffer 0 (op 5 `[1, 0]`).
   6. Set property 3 = 1.
3. **LED index = USB HID usage code** (F1 = 0x3A lit F1), the same ids Web Hub stores in lighting layers. The key map comes for free from the onboard layer files (`backups/…/file-28004.bin`, 103 keys; `file-28005.bin`, 126 keys).
4. **Mode 2 alone doesn't blank or freeze the keys.** It holds the onboard effect until a frame arrives.
5. **Release restores the onboard profile immediately** (static green + type-lighting red), with no replug.
6. **10 fps streaming doesn't affect typing** over 10 s.

## G3 crash test (D4), 17:17–17:20, watched on the second run

The 10 fps blue stream was force-killed (`taskkill /F`) mid-stream, so no release was sent.
- Runs: 17:17:59 (unwatched) and 17:20:09 (watched).
- Results, same both times:
  - **Property 3 still = 2 at least 20 s after the kill.** No device-side timeout.
  - **Keys keep showing the last frame (blue)** until released. Typing works throughout.
  - **A new session recovers it with no replug:** `Probe release` (new session id 2, set 3 = 1) restored the onboard green + red typing layer at once.
- **Consequence: D4 can't rely on the keyboard.** The helper must guarantee release itself:
  1. Release on every start, before anything else (covers a PC crash, a sleep, or a killed process found later).
  2. A separate tiny watchdog that releases when the helper process exits for any reason.
  3. Release on session end, sleep and shutdown notifications.

  Pass condition for D4 stays: onboard profile back within 2 s of the helper dying, no replug.

## Still unknown (G3)
- Whether a frame persists while holding mode 2 with no further frames (does the device need a stream or keep-alive?). In 2.2 a single frame held for 10 s with only pings.
- Index range: which of the 146 indices map to physical LEDs beyond the layer key lists (e.g. the 0x94–0x9F region, the top "strip" lamps). Map by sweeping indices with him watching.
- 20 fps × 10 min and 5 min of fast typing during streaming (D2, D3).
- Interaction with Web Hub open in a browser (D5), sleep/wake and replug (D5).
- Session expiry over long runs (reply byte 1 = 15) and recovery.
