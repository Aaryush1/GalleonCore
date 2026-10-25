# G0 spec: live per-key lighting on the Galleon 100 SD (keyboard part 1b1c:2b0c)

Synthesis of the four G0 notes in `research/`. Nothing here was tested on a device yet. Every byte below is a **hypothesis** until a G1/G2 log confirms it; sources are named so a wrong guess can be traced back.

## What everyone agrees on

| Fact | Sources | Confidence |
|---|---|---|
| Vendor command interface is MI_01, usage page 0xFF42, report ID 0 | Web Hub, GalleonCore, OpenRGB | High |
| Report length is 64 (Web Hub constant) **or 128** (GalleonCore on fw 2.x) | conflict | Read `OutputReportByteLength` from HID caps; never assume |
| Firmware 2.x uses the 4-byte v1.5 header `[sub=0, 0x01, session, op]`; v1.0 `08 ..` gets no reply | GalleonCore, Web Hub | High |
| Host session: op 0x1B payload `[1, 4 random bytes, 0]`, session id at reply byte 9; status at reply byte 4, data from byte 5 | Web Hub | High |
| Operating mode = property 3: 1 self-operated, 2 host-controlled. **3 and 5 = bootloader, 4 sleep, 6 limited host, 7 esports** | Web Hub, GalleonCore | High |
| Active profile = property 110 (index 0–4); brightness = property 2 (0–1000) | Web Hub | High |
| Web Hub has **no** live lighting path: every edit is a persistent file write | Web Hub, GalleonCore | High |
| Every other Bragi implementation's live path: mode 2 → open handle on resource **0x22** (or 0x01) → write color buffer (op 6, then op 7 continuations) → close (op 5) → mode 1 | ckb-next, OpenLinkHub, OpenRGB, iCUE LINK, galleon-deck | Medium (never shown on fw 2.x Galleon) |
| Resource 0x22: header `12 00` then interleaved RGB per LED. Resource 0x01: planar (all R, all G, all B), no header | OpenRGB, OpenLinkHub, ckb-next | Medium |
| galleon-deck (MIT, fw 3.70.1) lit the keys once with a planar 3×138-byte white buffer; other layouts **froze typing until unplug** | GalleonCore notes | Medium |
| Live frames > 25 fps stalled keystrokes on a Bragi keyboard | Delido / SignalRGB | Medium; cap at 20 fps |
| LED ids are USB HID usage codes (modifiers 224–230 → 105–111, FN 122); Galleon has 146 LED slots | Web Hub | High for layer files; unknown for live buffers |
| Starved software mode reverts to firmware lighting on some boards; ckb-next pings every 50 s | Delido, ckb-next | Low for Galleon |

## Never send (enforced by the transport allowlist)

- Property 3 with any value except 1 or 2.
- Op 0x0F (factory reset), 0x0B (create file), 0x0C (delete file), 0x0A (calibration), op 0x12 with a payload (pairing), op 0x1B stop.
- Ops 6/7 (buffer writes) to anything except a handle opened on a resource that G1 showed is **not** a stored file (0x22 / 0x01 candidates only).
- Property 0xFB–0xFF; any write to file 15 or any user file (≥ 28000).
- More than 20 frames/s.

## G1: read-only probe (risk ≈ none; he's nearby)

Tool: `lab/tools/Probe` (C#, .NET 10), raw HID through `CreateFile` + `HidD_*` on the MI_01 / 0xFF42 collection, shared access so Stream Deck keeps working. Every command and reply is logged to `lab/logs/` (gitignored).

Steps:
1. Enumerate HID collections for 1b1c:2b0c; print MI, usage page, usage, input/output/feature report lengths. **Decides 64 vs 128.**
2. Start a session (op 0x1B). Expect status 0 and a session id at byte 9.
3. Read properties with op 2 only:
   - 3 (mode), 110 (active profile), 2 (brightness);
   - 0x11 / 0x12 / 0x13 (firmware / PID / version, per OpenRGB and ckb-next);
   - 19 and 261, per Web Hub.
   Record the firmware version. galleon-deck's success was on 3.70.1.
4. Ping (op 0x12, empty).
5. Back up his onboard setup **read-only**: describe (op 9), then read (op 8) files 15, each profile file, and each lighting-layer file it lists. Unbind (op 5) in a `finally`. Save to `lab/backups/` (gitignored).
   - Open question: do reads work in mode 1? Web Hub always sets mode 2 first. **G1 stays in mode 1.** If reads fail there, record that and stop; moving to mode 2 is a G2 decision.
6. Describe (op 9) resources 0x22 and 0x01 **without writing**, to learn their sizes. 3 × 146 = 438 bytes for planar, 2 + 3 × 146 = 440 for 0x22.

Pass: all of the above logged, his profiles backed up, keyboard unaffected (typing check after the run).

Also in G1: the profile-switch fallback (test 0.14). This is the first write of any kind, so it runs only with his OK at the desk:
- mode 2;
- property 110 = target;
- mode 1.

## G2: first live write (he's at the desk, camera pointed at the keyboard, replug ready)

One variable per attempt, smallest change first. Each attempt follows the same steps:
1. Session.
2. Mode 2.
3. Open handle 0 on the resource.
4. Write one frame where **only F1 is green** and everything else is black.
5. Hold 2 s.
6. Close.
7. Mode 1.
8. Snap a photo.
9. Ask him to type a test line.

Attempt order:
1. Resource 0x22, `12 00` + interleaved RGB, LED order by HID usage (F1 = usage 58).
2. Resource 0x01, planar, same order.
3. Planar 3×138 (galleon-deck's working size).
4. Resend at 10 fps for 10 s (in case it needs a stream).

Abort an attempt the moment typing stops responding: unplug and replug, log it, and don't repeat that variant. Kill criterion: 3 desk sessions with no lit key → stop, and write up what was learned for upstream.

## Seeing the keyboard

`lab/tools/Snap` takes a still from any Windows camera. The PC's only camera (Integrated RGB) faces him. For G2 onward:
- **Recommended:** a USB webcam on a clamp above the keyboard (fixed framing, works for the 72 h soak).
- **Free alternative:** Phone Link "connected camera" with an Android phone propped above the keys.

Both appear to Snap as normal cameras.

## Contribution path

- Protocol findings: pull request to GalleonCore's `docs/protocol/`, rewritten to their conventions (SPDX headers, REUSE, cited sources). Live-lighting code goes to an OpenRGB fork (`CorsairPeripheralV2Controller`), as GalleonCore plans.
- `lab/` stays on our branch and is never proposed upstream.
