# Bragi direct-lighting: other open implementations (G0 research)

Scope: ckb-next, OpenLinkHub, Delido SignalRGB plugin, liquidctl/kernel/others. Not OpenRGB core, not GalleonCore.
Method: WebFetch of raw sources only; nothing cloned or run. The fetch tool summarises pages with a small model, so
byte layouts are as-reported and should be re-verified against raw files before relying on them (UNVERIFIED where thin).
Researched 2026-10-24.

## 1. Per-source findings

### 1a. ckb-next (C daemon): the most complete open Bragi implementation
License: GPL-2.0. Files: `src/daemon/{bragi_proto.h, bragi_common.c/h, led_bragi.c, device_bragi.c, usb_bragi.c}`.
- **Header/version:** v1.0-style. Byte 0 is `BRAGI_MAGIC 0x08` (device byte; child devices OR their id into it), byte 1 the opcode. No `0x01` version byte, no session id. Every request is a fixed `BRAGI_JUMBO_SIZE` buffer (value not seen; per-device `out_ep_packet_size`).
  `pkt = {BRAGI_MAGIC, BRAGI_SET, prop, 0, val&255, val>>8}`
- **Takeover (`setactive_bragi`):** (1) set prop `0x03` (MODE) = `0x02`; on failure logs "Failed to set device to SW mode"; (2) open handle 0 on resource `0x01` (`0x10` for monochrome); (3) if open reply is `0x01` or `0x06`, fall back to resource `0x22` ("alt lighting", interleaved), and also open handle 2 on resource `0x2e`.
  `handleopen = {BRAGI_MAGIC, BRAGI_OPEN_HANDLE, handle, resource & 0xFF, resource >> 8, 0}`. Open reply error `0x03` => close handle, retry once.
- **Write format:** start `{0x08, 0x06, handle, len32 LE}` (7-byte header) then data; continuation `{0x08, 0x07, 0x00, data}` (3-byte header, handle hardcoded to lighting handle). First packet carries `pktsize-7` data bytes, each continuation `pktsize-3`. First-chunk reply is NOT checked; continuation failures are ignored ("might be something recoverable"). Comment: "Device returns 0x03 on writes if we haven't opened the handle."
- **RGB layout:** resource `0x01` = PLANAR (all R, then all G, then all B; zones x3). Alt path (`0x22`) = INTERLEAVED with a 2-byte sub-header and `pkt1[7] = 0x12; // Some kind of header?`. Full frame every time, no partial updates. LED index = zone index directly, per-product fixed zone count (156 for K95 Platinum XT).
- **Keepalive:** poll thread every 50 s sends `{0x08,0x12}`, checks `resp[1]==0x12` ("CUE polls every 52 seconds"). Errors only logged.
- **Release (`setactive_bragi(HARDWARE)`):** close handle 0 and handle 2 first (`{0x08,0x05,0x01,handle,0}`), THEN set MODE=`0x01`. Poll thread is never stopped.
- **Dangers noted in code:** a commented-out packet "observed to break lighting on specific keys (such as the '3' key)"; generic handle 1 is "for quick read/writes. Do NOT leave it open"; init flips software then back to hardware while probing, ignoring errors.
- **Endpoints:** out `0x04`/in `0x84` default; K100, K70 TKL, K70 Pro, K55 Pro, K65 Mini use out `0x01`/in `0x82`.

### 1b. OpenLinkHub (Go, Linux), jurkovic-nikola/OpenLinkHub
License: GPL-3.0 (LICENSE confirmed). Keyboards in `src/devices/` (k65plusW/WU, k70core, k70pro, k100, k100airW...). No Vanguard or Galleon directory seen.
- **Header/version:** v1.0-style, no session or `0x1B`. 65-byte hidraw write: `[0]=report ID 0, [1]=0x08, [2..]=cmd bytes, payload`. Read 64 B. K100 uses 1024+1. Replies are never validated (only I/O errors).
  `bufferW[1] = 0x08` / `copy(bufferW[headerSize:...], endpoint)`
- **Takeover (k70core Init order):** `cmdSoftwareMode {0x01,0x03,0x00,0x02}` (set prop 3 = 2, LE16) -> `cmdActivateLed {0x0d,0x01,0x22}` (open handle 1, resource 0x22) -> sleep 500 ms -> get firmware `{0x02,0x13}` -> profiles -> key-assignment writes (open `{0x0d,0x02,0x02}`, write `{0x06,0x02}`, cont `{0x07,0x02}`, close `{0x05,0x01,0x02}`) -> keepalive ticker. No session command anywhere.
- **Color write:** K70 Core payload 371 B; buffer `[len+2 LE16][00 00][12 00][colors]` (377 B); split by `ProcessMultiChunkPacket(buf, 61)`; first chunk cmd `{0x06,0x01}`, rest `{0x07,0x01}`. k65plusWU is the same with handle 0 (`{0x06,0x00}`/`{0x07,0x00}`). Per-key `PacketIndex` offset with R at +0, G +1, B +2, i.e. INTERLEAVED. K100: colorPacketLength 581, 1021-byte chunks.
- **Rate/keepalive:** effect loop ~50 Hz (20 ms sleep) sending the full frame each tick; separate 20 s `{0x12}` ping ticker.
- **Release:** `Stop()` sends `{0x01,0x03,0x00,0x01}` (mode=hardware) then closes HID; lighting handle never closed. `StopDirty()` skips even that and leaves the device in software mode.
- **Dangers:** none documented; ignoring replies makes a failed takeover silent.
- These are older-generation Bragi devices (report ID 0 + 0x08 framing).

### 1c. Delido/signalrgb-plugins (JS): `Corsair_Bragi_Device.js`, `.md`, `dumps/corsair_keyboard/PROTOCOL.md`
License: not stated in fetched content (check repo before reuse). Only source with v1.5-looking packets `[reportID 0][sub][0x01][sess/conn][op]...`. Documented target: Vanguard Pro 96 (PID 0x2B0E), 1024-byte reports on EP 0x03/0x83, NOT 64 B.
- **Takeover:** 3 to 5 session handshakes `00 00 01 00 1b 01 <4-byte token>` (e.g. `...1b 01 14 64 35 52`, `...1b 01 3d 78 af 8b`) via write+read; then `SetMode(Software)` = set prop 3 = 2; on Vanguard also replays ~88 captured iCUE OUT packets (layout-config writes: keyboard "does not light up" without them), including a 969-byte write at `0x3D` and a `0xFB-0xFF` property block (author says harmless, UNVERIFIED). Commands without the handshake return status `0x0F`.
- **Opcodes (matches the task brief):** setProp 01, getProp 02, close 05, write 06, stream/continue 07, read 08, checkHandle 09, open 0D, ping 12, plus `0x15` confirmChange and `0x1B` session.
- **Color write (generic Bragi path):** open handle `00` on endpoint `0x01` (or `0x22` for "lighting controller", prefixed `0x12 0x00`). Start `[0,0,devID,0x02,0x06,handle=0,LedCount LE32,data]`; continue `[0,0,devID(|0x08 if streaming read),0x02,0x07,handle,data]`; close `[0,0,devID,0x02,0x05,1,handle]`. Chunk = write length minus header (constants 8/4 in source conflict with literal 10/6 byte headers, UNVERIFIED). Planar for 0x01 (`RGBData[ledIdx + ledSpacing] = col[1]`), interleaved for 0x22 (`RGBData[ledIdx*3] = col[0]`). Handle stays open; reopen if first write fails. Vanguard path: `writeEndpoint` to endpoint `0x22`, 419-byte frames with `0x12` sub-header, 25 fps cap, positions from `KEY_TABLE`.
- **Keepalive:** re-send software mode every 5 s (`PollDeviceMode`), ping every 50 s. Device does not latch frames; without fresh writes lighting reverts to firmware defaults.
- **Release:** paint black/shutdown colour, wait 20 ms, send `[0,0,1,3, 01,03,00,01]` (set prop 3 = 1) and `[0,0,1,3, 1b,02,00,00,00,00,03]` (session release, `1b` sub-op 2), verify with getProp 3 == 1, retry up to twice (second retry re-handshakes). Legacy `deviceID|0x08` hardware switch "has no effect on v2 firmware".
- **Dangers (author-observed):** >25 fps floods Windows overlapped I/O and the keyboard stops responding to keystrokes; mode toggling and config writes changed firmware state (needed Web Hub factory resets and bootloader resets); unknown replayed packets; `IsHandleOpen` unreliable on v2 firmware; LCD corner indicator does not render in Software mode.

### 1d. liquidctl, Linux kernel, Python "bragi", blogs
- liquidctl: only Corsair coolers/PSUs found; no Bragi keyboard code.
- Linux kernel: no Bragi lighting driver found.
- No standalone Python Bragi project or blog write-up surfaced. ckb-next source and the Delido dumps/PROTOCOL.md are the best public descriptions. OpenRGB CorsairPeripheralV2 (other agent) is the remaining one.

## 2. Comparison table

| Aspect | ckb-next | OpenLinkHub | Delido SignalRGB |
|---|---|---|---|
| License | GPL-2.0 | GPL-3.0 | not stated |
| Header | v1.0 `[08, op, ...]` | v1.0 `[00, 08, op, ...]` | v1.5-like `[00, sub, 01, sess, op...]` for session cmds |
| Report size | 64 (per-device) | 64 (K100: 1024) | 1024 (Vanguard) |
| Session `0x1B` | none | none | required, else status 0x0F |
| Software mode | set prop 3 = 2 | same | same (+ config replay on Vanguard) |
| Lighting handle/resource | h0 res 0x01, fallback 0x22 (+h2 res 0x2e) | h1 (or h0) res 0x22 | h0 res 0x01 / 0x22 |
| Write start | `06 hnd len32LE data` | `06 hnd` + `[len+2 LE16][00 00][12 00]` data | `06 hnd len32LE data` |
| Continue | `07 00 data` (3-byte hdr) | `07 hnd data` | `07 hnd data` |
| RGB layout | planar (0x01), interleaved (0x22) | interleaved | planar (0x01), interleaved (0x22) |
| Chunk payload | pkt-7 first, pkt-3 after | 61 B | write length minus header |
| Keepalive | ping 0x12 / 50 s | ping / 20 s, frames at 50 Hz | mode re-assert 5 s, ping 50 s |
| Frame rate | on change | 50 Hz | 25 fps cap |
| Release | close handles, then mode=1 | mode=1 only | black, mode=1, session release, verify+retry |
| Reply checking | partial | none | yes, with retry |

## 3. Consensus direct-lighting recipe
1. Where a session exists (v1.5), open it with `0x1B`; every later command carries the session id.
2. Set property 3 (mode) = 2 (software); verify by reading it back (Delido; ckb-next probes it).
3. Open a lighting handle with `0x0D`: handle 0 (or 1), resource `0x0001` (planar) or `0x0022` (interleaved + `12 00` sub-header). Try `0x01` first; if the open reply is `0x01`/`0x06`, fall back to `0x22` (ckb-next).
4. Write one frame: `0x06` carrying total length (LE32, or LE16 in OLH's 0x22 framing) in the first packet, then `0x07` continuations until the whole buffer is sent. Always a full colour buffer.
5. Resend continuously (25-50 Hz for effects, at minimum on every change). The device does not latch and reverts if starved. Ping `0x12` every 20-50 s.
6. Release: close lighting handle(s) `0x05`, then set prop 3 = 1, verify, then (v1.5) release session.
7. Keep the handle open across frames; reopen on write failure. Status `0x03` on write = handle not open.

## 4. Differences that matter for a 64-byte v1.5 device
- **Header overhead:** v1.5 header is 4 bytes. A start packet is roughly 4 + handle + len32 (about 9 bytes, so ~55 payload bytes), continuations 64 minus 4-5. Recompute from the real layout; do not copy 61 (OLH) or 1024-based constants (Delido).
- **Byte 0 meaning:** v1.0 `0x08` is a device byte; on v1.5 it is the subdevice byte, with separate version `0x01` and session bytes. Delido's `deviceID|0x08` trick is legacy and "has no effect on v2 firmware".
- **Session id in every frame, and session release at exit** (`1b 02`). ckb-next/OLH never do this (older firmware).
- **Chunk count:** ~470 B planar frame is ~9-10 reports at 64 B vs 1 at 1024 B. At 25 fps that is ~250 reports/s. Treat Delido's 25 fps cap as a hard ceiling until proven otherwise (their stall was on 1024 B overlapped I/O, but the failure mode was keystrokes dying).
- **Read every reply.** Codes seen: `0x03` handle not open, `0x0F` no session, `0x05` unsupported, `0x01`/`0x06` open failure/unsupported resource. OLH ignores them; ckb-next half ignores them.
- **Resource choice (0x01 planar vs 0x22 interleaved) is per product.** Use `0x09` checkHandle (length at reply bytes 5-8 in ckb-next) to size the buffer.
- **Newer devices may need config replay** (Vanguard lit nothing without iCUE layout writes). Risk for Galleon.
- **Mode re-assert cadence differs** (Delido 5 s vs ping-only elsewhere); unknown which the Galleon needs.

## 5. Hypotheses for the Galleon (ranked)
1. Most likely: `0x1B` session + prop3=2, open handle 0 on `0x22` or `0x01`, chunked `0x06`/`0x07` write works as in ckb-next/Delido; the 64-byte report only changes header math. Test: open both resources, log the status byte, `0x09` for length, write one known-colour frame.
2. Device needs extra takeover state (Vanguard-style layout/config replay or property block). Symptom: writes ACK status 0 but no light. Mitigation: capture iCUE traffic once.
3. Colours revert within seconds unless resent: build 20-25 fps resend, 5 s prop-3 re-assert, 50 s ping.
4. Resource `0x22` needs the `12 00` sub-header and `len+2` length variant (OLH style) while `0x01` is plain planar. Wrong choice shows as garbled colours or wrong keys, not an error.
5. Release order matters: close handles, then prop3=1, then session release. Skipping leaves the keyboard stuck in software mode (OLH `StopDirty` behaviour). Do it in a finally/atexit and keep a standalone hardware-mode recovery command.
6. Dangers: prop 3 = 3 is BOOTLOADER, so never write any value but 1 or 2 to it. Do not replay captures blindly (0x0F factory reset, 0x0C delete file, `0xFB-0xFF` property block). Delido needed Web Hub factory resets and saw keystroke stalls from too-fast writes.

## 6. Sources
- https://github.com/ckb-next/ckb-next/tree/master/src/daemon (bragi_proto.h, bragi_common.c/h, led_bragi.c, device_bragi.c, usb_bragi.c), GPL-2.0
- https://github.com/jurkovic-nikola/OpenLinkHub/tree/main/src/devices (k70core, k65plusWU, k100), GPL-3.0
- https://github.com/Delido/signalrgb-plugins (Corsair_Bragi_Device.js/.md; dumps/corsair_keyboard/PROTOCOL.md linked from README, raw URL returned 404), license not seen
- https://github.com/liquidctl/liquidctl (no Bragi keyboards)
- https://www.gamingonlinux.com/2025/03/ckb-next-to-configure-corsair-keyboards-and-mice-v0-6-1-out-now-with-new-hardware-support/page=1/

## 7. Gaps / follow-ups
- Read Delido `PROTOCOL.md` via the GitHub web UI or `gh api` (raw 404). Likely the best v1.5-with-session description.
- Raw-read ckb-next `bragi_common.h`/`usb_bragi.h` for `BRAGI_JUMBO_SIZE` and any version handling (not found by fetch).
- Confirm OLH `ProcessMultiChunkPacket` (in `src/common`) for exact handle-byte placement.
- Licenses are GPL: reimplement from the protocol facts, do not copy code.
