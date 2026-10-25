# G2 plan: first live write (needs Aaryush at the desk)

Builds on `G1-RESULTS.md`:
- Firmware 2.3.88.
- MI_01, 128-byte reports, v1.5 header `[0, 1, session, op]`.
- File reads work in mode 1.
- Full backup taken (`backups/20261024-202033`: profile list, profile, properties, assignments, both lighting layers).
- Resource **0x22 opens in mode 1 and describes as size 0** (a sink, not a stored file).
- Resource 0x01 does not exist (status 6).

## Goal

Make at least one key show a color sent from the PC without writing to storage, then hand control back. One variable changes per step. Each step has a pass, a stop and a recovery.

## Tool changes (all enforced in code)

`Probe` gains a `--g2` mode. The G1 allowlist stays, plus exactly:
- **op 1 set property 3, value 1 or 2 only.** Any other property or value is refused.
- **op 6 / op 7 (write) only on buffer 0, only while buffer 0 is bound to resource 0x22** (tracked by the tool). Length ≤ 3 × 146 + 2 bytes.
- Frame rate cap 20 / s (one frame = op 6 + continuations).
- **Release is unconditional:** every run ends with unbind buffer 0, then set property 3 = 1, in a `finally`. Ctrl-C does the same.
- If any reply has status ≠ 0, times out (3 s), or reports invalid state (byte 4 = 3): stop sending, release, exit.
- Every TX/RX logged. Optional photo via `Snap` after each step, if a camera is set up.

Write framing for 128-byte reports (Web Hub layout scaled up):
- First chunk, op 6: `[0, 1, s, 06, buf, len LE32, data(119)]`.
- Continuations, op 7: `[0, 1, s, 07, buf, data(123)]`.

## Steps (≈ 20 min, he types a test line after each)

| Step | What changes | Sent | Pass | Stop / recover |
|---|---|---|---|---|
| 2.0 | nothing (read-only repeat) | session, get 3, ping | mode 1, replies OK | n/a |
| 2.1 | **mode only** | set 3 = 2, wait 3 s, observe, set 3 = 1 | typing works in mode 2 and after; note what the keys show in mode 2 (dark? frozen on last colors? unchanged?) | typing stops → unplug/replug; G2 ends, write up |
| 2.2 | 0x22, interleaved with `12 00` header (OpenRGB / OpenLinkHub), **every LED the same color** (blue, so index order doesn't matter), 146 LEDs | mode 2, open 0x22, op 6/7 one frame (2 + 438 bytes), hold 3 s, unbind, mode 1 | keys turn blue, then return to his green | no change → 2.3; freeze → replug, skip remaining 0x22 variants |
| 2.3 | planar R…G…B, no header (galleon-deck), 146 LEDs | same | keys turn blue | no change → 2.4 |
| 2.4 | planar 3 × 138 (galleon-deck's exact size, its only success) | same | keys turn blue | no change → 2.5 |
| 2.5 | best of 2.2–2.4, resent at 10 fps for 10 s (some boards need a stream) | same, 100 frames | keys blue for 10 s, typing fine | lag while typing → stop |
| 2.6 | only if something lit: single-key test, LED index = HID usage of F1 (0x3A) | one frame | only F1 lights | wrong key → record which; mapping work goes to G3 |

**Kill criterion** (from the plan): no lit key after 3 desk sessions of variations → stop and write up for upstream. Not one: this is session 1.

## Recovery ladder

1. Replug the keyboard USB (expected fix for a freeze).
2. Web Hub → Device Settings → Reset (factory reset).
3. Restore his layers in Web Hub by hand from the decoded backup: Static RGB(18, 255, 5) and Type Lighting Key RGB(255, 41, 79) / (255, 38, 0), speed 5.
4. Corsair's documented B+L firmware recovery (Chrome, keyboard direct to the motherboard).

## What he needs to do

- Be at the desk for ~20 min with a free hand for the USB cable.
- Watch the keys at each step, then type the test line I give.
- Optional: a camera above the keys so I can check myself. Not required for G2, but it is for G3's longer runs.
