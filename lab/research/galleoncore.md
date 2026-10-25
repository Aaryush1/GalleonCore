# GalleonCore research (G0)

Source project: Paullux/GalleonCore (checked 2026-10-24, main at PR #54). Facts below are the project's own claims. "OBS" = they measured it, "INF" = they inferred it, "UNK" = unknown. Firmware matters: nearly all their keyboard data is **2.3.88** (they started on 1.4.73; LampArray descriptor data was identical on both).

## 0. Headline

- The keyboard's 108-lamp LampArray (MI_04) **acks every write but lights no keys**, on Windows (OpenRGB) and Linux (own libusb tool), fw 2.3.88, even with Bragi property 3 held at 2.
- The dial rings DO work, via LampArray (2 lamps, MI_03 of 2b18) and via a Stream Deck-protocol report `03 24 <led> r g b` (4 LEDs per dial).
- Nobody has captured or demonstrated a live (non-persistent) per-key stream on a Galleon. Web Hub only writes the saved profile to flash. Their roadmap calls keyboard RGB "blocked until a non-persistent write protocol is found".

## 1. Vendor HID interfaces, Bragi framing, properties, opcodes

### Interfaces (OBS via HidP_GetCaps; lengths include report ID)
Keyboard 1b1c:2b0c:
- MI_00: boot keyboard (COL01 in15/out2, COL02 in18/out2) and consumer COL03.
- **MI_01: usage page 0xFF42 usage 1, in 129 / out 129, unnumbered** (128-byte payload). Bragi/V2 control. Endpoints OUT 0x04 / IN 0x84.
- MI_02: 0xFF42 usage 2, in 65 (vendor events).
- MI_03: mouse. **MI_04: usage page 0x59 (LampArray), feature 51**, no endpoint.

Stream Deck 1b1c:2b18: MI_00 consumer/vendor (in 512, out 1024, feature 32: the Stream Deck protocol), MI_01 numpad keyboard, MI_02 consumer (dials), MI_03 LampArray (feature 51). Internal hub 1b1c:2b19.
> "A major version change may alter interfaces, report IDs and lighting behaviour: record the version with every measurement."

### Framing (OBS from usbmon/USBPcap and the Web Hub JS bundle)
- Request `00 01 <session> <cmd> <args...>`; answer `00 00 00 <cmd> <status/data>`; zero-padded to 128 bytes. Byte0 = report number 0, byte1 = 1 request, byte2 = **session ID** (0 = none), byte3 = command.
- Client validation: answer byte1 = 15 means session expired; byte4 = 3 means invalid state. Status at byte 4, data from byte 5.
- This is NOT the `08 02 <prop>` form used by galleon-deck/OpenRGB: on fw 2.3.88 that form got **no reply in 500 ms**.
> "with that format the keyboard did not answer at all on this firmware"
- Linux hidraw pitfall: write 129 bytes (leading 0) or the first frame byte is dropped. Do not read the hidraw while a WebHID page is open (every reader gets a copy of every answer).

### Opcodes (names from Web Hub client; numbers match OpenRGB corsair_v2_cmd)
| Hex | Meaning | Args |
|---|---|---|
| 01 | set property | id u16le, value u32 |
| 02 | get property | id u16le |
| 03 | get several props | count, ids |
| 05 | unbind buffer | count, buffer idx |
| 06 | write buffer, begin | buf idx, total len u32le, data |
| 07 | write buffer, continue | buf idx, data |
| 08 | read buffer | buf idx |
| 09 | describe buffer (size) | buf idx |
| 0a | calibration control | 2 bytes |
| 0b / 0c | create / delete file | file id u16le |
| 0d | open file into buffer | buf idx, file id u16le |
| **0f** | **factory reset** | 1 byte. NEVER |
| 12 | ping / pairing (Web Hub ~30 s; `02 e1` poll ~2.4 s) | none |
| 1b | host software session: start `01, 4 random bytes, 00` (answer byte 9 = session ID); stop `02, 4 zero bytes, sessionID` | |

Model: a small file system. Open file into a buffer, describe (size), read or write, unbind.

### Properties
Read OK (status 0): 0x01=4, 0x02=0x3e8 (1000), 0x0a=5, 0x13=0x580302 (bytes 02 03 58 = fw 2.3.88), 0x41=2, 0x6e=1. 0x05 needs extra arg 01 (Web Hub gets 0x0501), else status=5. (galleon-deck on fw 3.70.1: 0x11 VID, 0x12 PID, 44 props answer, none an LED count.)
- **Property 3 = operating mode** (Web Hub client names): 1 SELF_OPERATED, 2 HOST_CONTROLLED, **3 BOOTLOADER**, 4 SLEEP, **5 MCU_BOOTLOADER**, 6 LIMITED_HOST_CONTROLLER, 7 ESPORTS. OpenRGB calls it "render mode". Set = `00 01 00 01 03 00 <v>`. 1 and 2 were accepted (reply `00 00 00 01 00`), reads identical in both. Only 1/2 were ever written by them; 3 and 5 enter a bootloader. 4/6/7 never tried.
- Files: 0x000f = 8 bytes `13 00 02 00 60 6d 65 6d`. **0x0022** (OpenRGB LIGHT_CTRL2, the one galleon-deck opens) exists, size 8, all zero while idle, purpose unknown. Profile nodes 0x6dNN (0x6d65, 6d67, 6d6a). Profile record is 0x89 bytes.

## 2. Experiments to light keys

| # | Who / fw | Sent / opened | Result |
|---|---|---|---|
| A | galleon-deck, **fw 3.70.1**, iface 1 | `08 01 03 00 02` (software lighting), `0d 00 22` start, `06`/`07` block writes, `05 01 00` stop | White worked once with a **planar 3x138-byte buffer**. Other layouts **froze the whole keyboard incl. typing until unplugged**. 193-LED buffer crashed on 5th packet. A later 138-LED planar write froze it after returning to hardware mode. |
| B | GalleonCore, fw 2.3.88, Linux libusb | LampArray control (id 6 on 2b0c / 229 on 2b18) AutonomousMode=0; multi-update (id 4 / 227), 8 lamps per report, full 108-lamp frames at 3 ms pacing, complete flag on last batch | 2b18: dial lamps light. 2b0c: every report acked `ok`, **no key changes**. LCD key backlight unaffected on both. |
| C | same + OpenRGB plugin, all LEDs one colour | as B | same: dials yes, keys no |
| D | same | `bragi-probe --hold 100` kept property 3 = 2 while `lamparray-test-lamp keyboard sweep` ran (lamps 0-107 in turn, control 6 = 0 first) | **Still no key changed.** Rules out hardware render mode and Windows Dynamic Lighting as the cause. |
| E | Windows OpenRGB ("Dynamic Light"), fw 1.4.73 and 2.3.88 | whole-zone colour | dials yes, keys no. Lamps 94-107: white on last LED entries, nothing lit (not exhaustive). |
| F | Web Hub capture (usbmon, USBPcap) | static colour x3-4, rainbow, green | Never streams. Each change = one burst (~29 frames each way, ~1 s): `01 03 00 02`, open/describe/read/unbind of profile nodes, `06 01 89` + `07` profile write, `01 03 00 01`. **Persistent profile write; do not replay.** |
| G | read-only probes (`bragi-probe`, `bragi-read-file`) | props, open/describe/read/unbind files 0x0f, 0x22, 0x26, no session | OK, no side effects reported |

### Typing freeze incident
- It is **galleon-deck's** (NLMP-DDHS/galleon-deck docs/PROTOCOL.md, MIT, fw 3.70.1), not GalleonCore's. GalleonCore only cites it: "A colour write with the wrong buffer layout froze typing until the keyboard was unplugged."
- Sent: the sequence in row A with wrong buffer layouts. Recovery: **unplug the keyboard**. No more detail (exact bad bytes) is documented.
- GalleonCore never reproduced a freeze: they never sent a colour block through Bragi (rule in CLAUDE.md: "RGB writes can freeze the keyboard until unplugged. Never send anything that writes to flash").
- The property-3 switch 1 to 2 to 1 was done on Linux with no freeze (no colours sent; visual effect in mode 2 not recorded).

### The profile record (what Web Hub writes)
- Resource 0x89 (137) bytes: header `7e 20 01 00 00 00 01`, four colour bytes, count `7d` (125), then 125 one-byte key IDs = **HID keyboard usage numbers** (04..45, 47..65, 69..6f, 73..77, 7a, 7c, 81..87, 8b..91). The firmware lighting engine addresses 125 keys by usage ID, unlike the LampArray's 108 indices.
- Colour bytes: one doc reads `ff, B, G, R` (red `ff 00 00 ff`, green `ff 00 ff 00`), the other `ff 4f 29 ff` / `ff 32 dd 5d` as R,G,B at offsets 17-19. Order not settled. A byte at offset 11 of the last 06 write changes each burst (counter/checksum?). Animated-effect payload undecoded.

## 3. LampArray findings (OBS, identical on fw 1.4.73 and 2.3.88)
- Keyboard 2b0c MI_04: report IDs 1-6 (1 attributes, 2 lamp-attr request, 3 response, 4 multi-update, 5 range-update, 6 control). **108 lamps**, 440x150x25 mm, kind 1, **min update interval 3 ms**. All lamps: purpose 1 (control), RGB 8-bit, one intensity level, programmable.
- Stream Deck 2b18 MI_03: IDs 224-229, **2 lamps** (X 386.0 and 418.3 mm, Y 19), 20 ms.
- Multi-update is 51 bytes with ID: count u8, flags u8 (1 = update complete), eight lamp IDs u16, eight (R,G,B,intensity). Control report: 1 byte AutonomousMode (0 = host controls, 1 = device lighting).
- **Keys 0-93, other 94-107**, grouped by Y: 45 mm: 0-10, 66-70 (Esc, F-row, 3 keys); 65: 11-21, 73-78; 85: 22-32, 80-85; 105: 33-43, 88-90; 125: 44-54, 71, 91-93; 145: 55-65, 72, 79, 86, 87 (59-61 share the space bar). Full-size layout without numpad. IDs 94-107: 14 lamps at Y=0, 7 distinct X (77, 90, 185, 198, 238, 289, 345 mm), two each. Positions only, no key labels. Data: docs/protocol/lamps-2b0c.csv, lamps-2b18.csv.
- 94-107 hypothesis (INF): ambient bar, 7 segments Web Hub draws above the F-row. A white test lit nothing, so maybe placeholders.
- Parser quirk: the keyboard answers with the wrong report-ID byte (`04` for report 1, `02` for report 3); the Stream Deck part is right. Check the ID byte.
- Linux: usbhid doesn't bind the LampArray ifaces (no interrupt endpoint), so no hidraw. Use libusb control transfers GET/SET_REPORT (feature), wIndex = interface. On Windows the HID feature API works (Web Hub scripts use it).
- LampArray writes are volatile; no freeze seen.

## 4. Dials and LCD
- Dial rings via LampArray: lamp 0 (X=386) presumed left dial, never verified.
- Dial rings via Stream Deck protocol (from rust-elgato-streamdeck PR 63, confirmed by GalleonCore with colours seen on hardware): 32-byte feature report `03 24 <led> <r> <g> <b>`, 4 LEDs per dial, `led = (1 - dial)*4 + segment` (dial 0 = LEDs 4-7, dial 1 = 0-3). Needs the 500 ms keep-alive `03 27` (host mode). LampArray exposes only one lamp per ring.
- Contention: Windows Dynamic Lighting and OpenRGB "Dynamic Light" compete with the Stream Deck software for the rings; no arbitration exists.
- LCD module (2b18 MI_00, module fw 3.06.005, OBS): keep-alive feature `03 27` every 500 ms, else it falls back to standalone within about a minute; brightness `03 08 <0-100>`; reset `03 02`. Screen 720x384 JPEG, header `02 0c x y w h last part len 00`, 1008 B JPEG/report; 12 keys 160x160 JPEG, `02 07 <key> <last> <len u16le> <part u16le>`, 1016 B/report; output report id 2, 1024 bytes. **Pictures appear one frame late unless sent twice** (keep-alive doesn't flush). Input: keys `01 00 0c 00` + 12 bytes; dials `01 03 03 00 00 d0 d1` press, `01 03 03 00 01 d0 d1` rotate (signed). LCD key backlight is not a lamp.

## 5. Their roadmap and hypotheses
- ROADMAP.md item 6: keyboard RGB "remains on the OpenRGB fork side, blocked as long as the non-persistent write protocol is not found". Plan: Corsair V2 controller in OpenRGB (GPL-2.0+), registered at runtime by a plugin. Not developed in GalleonCore.
- Open questions listed in docs/protocol: why the 108 lamps ignore LampArray (Windows DL and render mode now ruled out; hardware profile suspected); what 94-107 are; layout of a software lighting stream (planar vs interleaved, framing), needs a capture, none exists because iCUE doesn't detect this keyboard; which dial is which lamp; counter byte and cmds 08/09.
- "Not tested: other firmware versions, and the vendor interface (usage page 0xFF42) that galleon-deck uses, which is the risky one."
- Repo has Discussions disabled, no issues, 54 PRs all closed. PR #5 (galleond themes/dials/screensaver, "Protocol notes and measurements") adds nothing beyond the docs above. PR #31 (Windows HID startup workaround) was closed unmerged.

## 6. License
GalleonCore: **GPL-3.0-or-later** (docs the same; artwork CC-BY-SA-4.0; REUSE). galleon-deck: MIT. OpenRGB: GPL-2.0+. Use facts, do not copy GPL code.

## What NOT to send
- Opcode `0f` (factory reset). Property 3 = 3 or 5 (bootloaders). Avoid 4/6/7 until understood.
- Any `06`/`07` block write to profile nodes (0x6dNN), or replay of Web Hub bursts (flash).
- galleon-deck's software-lighting sequence (`08 01 03 00 02`, `0d 00 22`, block writes) with a guessed layout (froze typing; even its "working" planar white is unverified on 2.x).
- `08 02 <prop>` framing (unanswered on 2.x); use `00 01 00 ...`.
- File create/delete (0b/0c); session stop `1b 02` with a bad session ID; ping/pairing `12` (untested).

## Promising next experiments (ranked)
1. Read-only analysis of the Web Hub JS bundle (corsair.com/web-hub) for a live-lighting/preview path and for what host session `1b`, property 3 = 2 or 6, and file 0x22 are used for. Zero device risk.
2. Capture Web Hub with the colour picker live/dragging, or any "preview" feature, on USBPcap/usbmon; look for traffic that is not a `06 01 89` profile write and for `0d .. 22` opens.
3. Read-only: open/describe/read file 0x22 and 0x26 with property 3 = 2, and again inside an open `1b` host session; compare with the all-zero idle value.
4. LampArray keys retried inside a Bragi host session (`1b` start) with property 3 = 2 (they tried mode 2 without a session). Then mode 6 only if 1-3 suggest it.
5. LampArray variants: range-update (id 5), intensity values, flags without "update complete", read back lamp state after a write; compare behaviour on fw 1.4.73 if reachable (they tested keys on Linux only on 2.x).
6. Identify lamps 94-107 one pair at a time via LampArray.
7. Dials: reuse their confirmed `03 24` report plus `03 27` keep-alive for ring lighting.

## Unknowns
- Any live per-key write that works on Galleon fw 2.x; whether keys are live-controllable at all.
- Meaning of file 0x22, the per-burst counter byte, cmds 08/09 semantics beyond file read, profile colour byte order.
- Mapping between the 125 usage-ID keys and the 108 lamp indices; identity of lamps 94-107.
- Whether fw 3.x (galleon-deck's 3.70.1) behaves differently; any freeze recovery beyond unplugging.
- Which dial is LampArray lamp 0; visual effect on keys of mode 2.

## Sources
- https://github.com/Paullux/GalleonCore (LICENSE GPL-3.0; REUSE.toml)
- https://raw.githubusercontent.com/Paullux/GalleonCore/main/docs/protocol/README.md
- https://raw.githubusercontent.com/Paullux/GalleonCore/main/docs/protocol/webhub-lighting-analysis.md
- https://raw.githubusercontent.com/Paullux/GalleonCore/main/docs/protocol/webhub-capture-2026-10-18.txt
- https://raw.githubusercontent.com/Paullux/GalleonCore/main/docs/protocol/lamps-2b0c.csv (and lamps-2b18.csv)
- https://raw.githubusercontent.com/Paullux/GalleonCore/main/ROADMAP.md, docs/ROADMAP.md, CLAUDE.md
- https://github.com/Paullux/GalleonCore/pull/5
- Tool sources named but not read: tools/linux/{bragi-probe.c, bragi-read-file.c, lamparray-probe.c, lamparray-test-lamp.c}, tools/windows/lamparray-*.ps1
- https://github.com/NLMP-DDHS/galleon-deck/blob/main/docs/PROTOCOL.md (MIT; origin of the typing-freeze report)
- https://github.com/Julusian/node-elgato-stream-deck ; rust-elgato-streamdeck PR 63 (cited via GalleonCore)
