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

## Known limitations of the follow-up

- If Web Hub connects while we're idle and *not* holding the keys, our next frame takes host mode and can override what Web Hub set. We yield at Web Hub's next poll (≈ 2.5 s), but its preview may show our last frame until it's been closed for 60 s. Fixing that would mean sending a command while Web Hub is active, which rule 2 forbids.
- Another host that sits in mode 2 for 60 s with no traffic at all would be overridden. That doesn't happen on this machine (Web Hub polls every ~2.5 s), but it's untested against iCUE.
- If Guard fires while Web Hub is open, it sends one session start and one mode read.

## Still open

- The four follow-up desk checks above.
- D7 72 h soak: with the desk-lights helper, now that yield and reconnect are in.
