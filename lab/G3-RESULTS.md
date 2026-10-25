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

## Still open

- Top-row index map (follow-up sweep of 0x81–0x91).
- D2 rate rerun at 20 fps.
- D7 72 h soak: after the yield + reconnect rules are in, with the desk-lights helper.
