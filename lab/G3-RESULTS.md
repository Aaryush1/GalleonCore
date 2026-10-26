# G3 results: desk checks (2026-10-25, 17:28–17:46 CDT)

Aaryush at the desk, no camera. Tools: `Probe g3-*` through `GalleonLink.Keyboard`, plus `Guard`. Logs in `logs/` (gitignored).

| Check | Result | Evidence |
|---|---|---|
| **D4 crash, with watchdog** | ✅ **Pass.** Keys back to his green + red typing within **0.38 s** of a deliberate crash, no replug. | Crash (FailFast, no release) 17:28:52.743. Guard saw the exit 17:28:52.861, fresh session, mode 2 found → released, mode 1 confirmed 17:28:53.127. He saw blue then green "quick". |
| **Index sweep** | ✅ The 18 LED indices his layers don't use as keys (00–03, 66–68, 70–72, 78, 79, 7B, 7D–80, 88) **light nothing**. | Three 6 s magenta groups; "Nothing lit… the keyboard was just dark" (so the black background reached every key). |
| **Top row** | ⚠ New: the **keyless top row by the Corsair logo is addressable**: the endurance band crossed it. So some of the 128 indices in his layers are that row, not keys. | His report. Which indices: follow-up sweep over 0x81–0x91 needed. |
| **D2 + D3 endurance** | ✅ **Stability pass**, ⚠ **rate miss.** 10 min, 7 657 frames, **0 failed writes, 0 timeouts, no freeze trip**; longest gap between replies 0.216 s. He typed fast inside the run: **no lag, no dropped keys**. Back to green at the end. **Average only 12.8 fps vs 20 required**: the test loop slept 50 ms *after* each write. Loop fixed to deadline pacing; rerun D2 for the rate. | Run 17:30:13–17:40:13. |
| **D5 sleep / wake** | ✅ Pass. | `power: suspend, releasing` at 17:42:02, mode 1 confirmed. Keys went dark while asleep (USB suspended); a keypress woke the keyboard into its onboard green (correct: we had released). After wake the run re-took host mode at 17:42:24 and the band resumed. |
| **D5 replug** | ✅ Safe. ⚠ No auto-reconnect yet. | Unplug at 17:44:04 → "device not connected" twice → freeze policy tripped → run ended cleanly. After replug the keyboard is in **mode 1 by itself** (power cycle resets host mode); keys green, typing fine. The test build doesn't reconnect; the helper must (`DeviceWatcher` or periodic enumerate). |
| **D5 Web Hub opened mid-run** | ❌ **Conflict found.** Keys back to green with **blue flicker ("fighting")**; typing fine; Web Hub connected normally. | Web Hub sets mode 1. Our writes keep returning status 0 (1 785 frames, no error, no session expiry), so the device gives **no error signal**. Web Hub's own traffic appears on the shared MI_01 input as "not our reply" every ~2.5 s. Stopped by killing the run and `Probe release` (mode was already 1). |

## Design rules this adds (for GalleonLink before the desk-lights helper uses it)

1. **Yield, never fight.**
   - While in host mode, re-read property 3 about once a second. If it isn't 2, someone else took the keyboard: stop sending and don't re-assert.
   - Treat any unsolicited reply on MI_01 ("not our reply") as another host present, and yield the same way.
   - Resume only after 60 s with no foreign traffic and the mode still 1.
2. **Protect Web Hub's saves.** Web Hub matches replies only by echoed opcode, so our reply flood could confuse a Web Hub save in progress. Yielding at the first foreign reply covers this.
3. **Reconnect after replug:** re-enumerate on device arrival. Release-on-connect (already in `Keyboard.Connect`) handles any leftover state.
4. **Keep the 20 fps cap**, paced by deadline. Rerun D2 to confirm the rate.

## Follow-up built (offline, 2026-10-25; self-test 84/84, no device writes)

- **Yield, never fight** (`YieldPolicy`, `Replies.Classify`):
  - While holding the keys, `Keyboard` re-reads the mode about once a second, both in `Show` and from a background supervisor when idle.
  - Any reply nobody asked for counts as another host. A late reply to our own timed-out command is "stale", not foreign.
  - Either sign means yield: no frames and no commands at all, so Web Hub's replies can't be confused. Sleep, sign-out and Dispose skip the release while yielded, because an unbind could break the other host's file transfer.
  - While yielded, the supervisor only *listens*, in 2 s windows.
  - After 60 s with no foreign traffic, it reads the mode. If it's 1 or 2 (2 = our own leftover frame), it releases and allows frames again. If it can't read the mode, it waits another 60 s.
- **Listen before touching:** `Connect` listens 3 s first. If another host is talking, it starts yielded and sends nothing. `Guard` passes zero, so it stays fast.
- **Reconnect after replug:** an I/O failure where the keyboard is no longer enumerated now means "unplugged", not a freeze.
  - The supervisor re-enumerates every second. On return it runs the same connect path (listen, session, release-if-needed) and resets the freeze policy.
  - A freeze trip is still final until a replug.
- **Session expiry** (reply byte1 == 15) now raises `SessionExpiredException`. `Keyboard` starts a new session and re-takes host mode; it counts toward the freeze policy.
- **Rate fix:** the limiter stamped each frame when its write *finished*, so a caller pacing at exactly 50 ms lost every other frame (part of the 12.8 fps). It's now a token bucket: 20/s on average with a burst of 2. A greedy caller is still held to ≤ 202 frames in 10 s, and 50 ms pacing with 15 ms jitter loses nothing.

## Follow-up desk checks (≈ 15 min, he watches)

| Check | Command | He does | Pass |
|---|---|---|---|
| Top-row map | `Probe g3-toprow` (default 0x81..0x91) | 8 s all magenta: what lights? Then one index added every 2 s, newest blue: where does each new light appear? | a usage → strip-position map (or "not this range": rerun with another range) |
| D2 rate | `Probe g3-band 2` | glance | log: **≥ 19.5 fps** while live, 0 freeze trips |
| Web Hub yield | `Probe g3-band 5`, open Web Hub mid-run, close it ~1 min later | watch | band stops within ~3 s, keys show his profile with **no flicker**, Web Hub works; band returns ~60 s after closing Web Hub |
| Replug reconnect | same run: unplug, wait 5 s, replug | watch | band comes back within ~5 s of replug, no restart of the tool |

## Follow-up desk results (2026-10-25, 17:58–18:11 CDT)

| Check | Result | Evidence |
|---|---|---|
| **Top-row map** | ⚠ **Partly mapped.** **0x81 = Copilot key.** 0x82 = **left end** of the strip, and the next one or two indices step right one LED at a time. From about 0x84/0x85 on, a single index lights a **section** (middle → right end), and those sections shrink as the index rises. So the strip isn't one LED per index past the first few. Likely the firmware groups the strip into zones. 0x8F–0x91 are at the right end. | `g3-toprow` (all + cumulative fill), `g3-toprow 8F 91`, `g3-toprow 82 91 one` (one at a time, his narration). Exact per-index segments not pinned down by eye. |
| **D2 rate** | ✅ **Pass.** While live: **232 full seconds at exactly 20 frames/s, 23 at 19**, 0 failed writes, 0 timeouts, 0 freeze trips. | Frame-begin (op 6) count per second from the log (5 124 frames). The tool's own summary line said 32.8 fps because it left write time out of "live" time; fixed. |
| **Web Hub yield** | ✅ **Pass.** He opened Web Hub ~1 min in. **Yielded at 18:06:31** on Web Hub's first reply. His report: keys **green, no flicker, Web Hub worked normally.** Nothing sent for 89 s. **Resumed 18:08:00**, exactly 60 s after Web Hub's last traffic: released (mode 1 confirmed), then re-took host mode. He'd closed the tab soon after opening; the band came back "pretty soon after". | log `g3-g3-band-20261025-180530.log`: 111 foreign replies, all inside the yielded window. |
| **Replug reconnect** | ✅ **Pass.** Unplug seen 18:08:34.558 as "unplugged" (not a freeze). Keyboard back in enumeration 18:08:44; after the 3 s listen, session + band back **18:08:47**, with no restart. He saw the band return. | same log |
| Band order | ℹ Not a bug. The band walks LED indices in USB key-code order (A, B, C…, then 1–0), so letters jump around the board while the number row runs left to right. Physical order is the desk-lights keymap's job. | his report |

Cosmetic: during reconnect the state reads "live" ~3 s before the session starts (the 3 s listen). No frames go out in that time, because the supervisor holds the I/O lock.

## Known limitations of the follow-up

- **Web Hub left open but idle:** Web Hub stops polling when idle. After 60 s of that, the link resumes **while Web Hub is still open**. It yields again at Web Hub's next command, but Web Hub's preview may show our frame until the link releases after the next 60 s of quiet. Not tested at the desk. Options for the helper: a longer quiet period, or not resuming while a Web Hub tab exists (it can't see tabs directly). **Decided 2026-10-25 (#19): keep 60 s**; he closes Web Hub quickly.

- If Web Hub connects while we're idle and *not* holding the keys, our next frame takes host mode and can override what Web Hub set. We yield at Web Hub's next poll (≈ 2.5 s), but its preview may show our last frame until it's been closed for 60 s. Fixing that would mean sending a command while Web Hub is active, which rule 2 forbids.
- Another host that sits in mode 2 for 60 s with no traffic at all would be overridden. That doesn't happen on this machine (Web Hub polls every ~2.5 s), but it's untested against iCUE.
- If Guard fires while Web Hub is open, it sends one session start and one mode read.

## Still open

- Exact top-strip segments for 0x84–0x91 (needed for day progress, #22; he says when, #24): `Probe g3-toprow 82 91 one 5`. **Web Hub source checked offline (2026-10-25):**
  - For the Galleon (`kb2B0C`) it knows `ledCount: 146` and the Copilot key at **index 129 (0x81)**, which confirms the desk finding.
  - **Correction (second grep):** its key table names **seven "Edge" LEDs: Edge1–Edge7 = codes 139–145 (0x8B–0x91)**. So Web Hub treats the end of the strip as 7 edge zones, probably 2 physical LEDs each. 130–138 are named GameMode, Digit0–5, Copilot and StreamDeck in a shared table, which may belong to another model. That still doesn't explain the sections seen at 0x84–0x8A.
  - The desk pass settles it: one index alone, 5 s each.
- ~~Web Hub left open and idle~~: accepted as is (#19).
- D7 72 h soak: with the desk-lights helper, now that yield and reconnect are in.

## Typing-glow demo (#21), 2026-10-25 19:50

`Probe g3-typing 2`: teal base, the pressed key glows white and fades over 350 ms, drawn by the helper from Raw Input (key codes never logged).

- **455 presses, all on mapped keys, 0 errors. Press → on the keys: p50 31 ms, p95 67 ms, max 94 ms** at the 20 fps cap.
- His verdict: "the delay feels fine… very high quality, possibly even more responsive than Galleon's hub." He likes teal → white (open to other colors). **The fade is too short.** Rerun with a longer fade: `Probe g3-typing 2 <fadeMs>`.
- **Decision #21: the helper draws the typing glow** (the onboard type lighting stays off while the helper holds the keys).
