# Protocol notes

Facts about the Galleon 100 SD USB protocol, each with its source.

## Sources

- node-elgato-stream-deck (Galleon support since v7.5.0): https://github.com/Julusian/node-elgato-stream-deck
- galleon-deck `docs/PROTOCOL.md`: https://github.com/NLMP-DDHS/galleon-deck/blob/main/docs/PROTOCOL.md

## Stream Deck module (`1b1c:2b18`)

- Host mode only while a keep-alive feature report is sent every 500 ms. (galleon-deck)
- Key and screen images are JPEG. (galleon-deck)
- Screen resolution: **unconfirmed**.

## Keyboard part (`1b1c:2b0c`)

- RGB uses Corsair's "V2"/Bragi protocol on USB interface 1. (galleon-deck)
- Read-only queries are safe. A colour write with the wrong buffer layout froze typing until the keyboard was unplugged. (galleon-deck)

## USB interfaces as declared by the hardware

**Firmware of the Windows measurements below: keyboard v1.4.73** (as shown by the official update tool, 2026-10-18). The keyboard was then updated to **v2.3.88**, and the three read-only scripts gave identical results (same interfaces, 108 and 2 lamps, same CSV). Everything measured on Linux (next sections) was done on v2.3.88, which the Web Hub capture confirms through property 0x13. A major version change may alter interfaces, report IDs and lighting behaviour: record the version with every measurement.

Source: `tools/windows/hid-caps.ps1` on Windows 11, 2026-10-18, a Galleon 100 SD plugged in and idle (numpad mode). It opens each HID interface with zero access rights and reads the capabilities the device declares (`HidP_GetCaps`): nothing is sent to the device. Lengths include the report ID byte. Device serial numbers are deliberately not recorded.

Keyboard part `1b1c:2b0c`:

| Interface | Usage page / usage | Input | Output | Feature | Notes |
|---|---|---|---|---|---|
| MI_00 COL01 | 0x0001 / 0x0006 keyboard | 15 | 2 | 0 | |
| MI_00 COL02 | 0x0001 / 0x0006 keyboard | 18 | 2 | 0 | |
| MI_00 COL03 | 0x000C / 0x0001 consumer | 3 | 0 | 0 | |
| MI_01 | 0xFF42 / 0x0001 vendor | 129 | 129 | 0 | The vendor interface galleon-deck calls "interface 1" (RGB, Bragi/V2): 128-byte payload plus report ID |
| MI_02 | 0xFF42 / 0x0002 vendor | 65 | 0 | 0 | |
| MI_03 | 0x0001 / 0x0002 mouse | 7 | 0 | 0 | |
| MI_04 | 0x0059 / 0x0001 | 0 | 0 | 51 | Usage page 0x59 is the standard HID *Lighting and Illumination* page (LampArray, used by Windows Dynamic Lighting) |

Stream Deck module `1b1c:2b18`:

| Interface | Usage page / usage | Input | Output | Feature | Notes |
|---|---|---|---|---|---|
| MI_00 | 0x000C / 0x0001 consumer | 512 | 1024 | 32 | Large reports, the only interface that could carry images and key events: **to confirm with a capture** |
| MI_01 | 0x0001 / 0x0006 keyboard | 20 | 2 | 0 | Numpad mode |
| MI_02 | 0x000C / 0x0001 consumer | 3 | 0 | 0 | |
| MI_03 | 0x0059 / 0x0001 | 0 | 0 | 51 | Also a LampArray interface |

USB product strings (Windows): `1b1c:2b0c` = "CORSAIR GALLEON 100 SD Mechanical Gaming Keyboard", `1b1c:2b18` = "CORSAIR GALLEON 100 SD".

OpenRGB on Windows (user's installed build, version not recorded) lists two entries for the keyboard: "CORSAIR GALLEON 100 SD Mechanical Gaming Keyboard" and "Dynamic Light". OpenRGB's source (checked at commit 96e198a, 2026-10-16) has no Galleon-specific controller and no Corsair V2 entry for `2b0c`, but it has a generic `HIDLampArrayController` that reads usage page 0x59 from the HID report descriptor. So both entries are most likely LampArray devices, one per USB part. Which entry belongs to which part is not confirmed.

Also present: a generic USB hub `1b1c:2b19` (the keyboard's internal hub).

### LampArray attributes (measured)

Source: `tools/windows/lamparray-attributes.ps1`, 2026-10-18. The only request sent is a feature report GET of the LampArray attributes report; its ID and field positions come from the device's own descriptor. The field order in the answer is: lamp count (16 bits), bounding box width, height, depth in micrometres, LampArray kind, minimum update interval in microseconds (32 bits each).

| Part | Interface | Attributes report ID | Lamps | Bounding box (mm) | Kind | Min update interval |
|---|---|---|---|---|---|---|
| Keyboard `2b0c` | MI_04 | 1 | **108** | 440 × 150 × 25 | 1 (keyboard) | 3 ms |
| Stream Deck `2b18` | MI_03 | 224 | **2** | 450 × 55 × 25 | 1 (keyboard) | 20 ms |

Declared feature report IDs: keyboard 1 to 6, Stream Deck 224 to 229 (same layout, offset). In the standard layout these are attributes, lamp attributes request and response, multi-update, range-update and control reports.

Quirk to handle in the parser: asked for report 1, the keyboard part answered with an ID byte of `04` and a valid payload (the Stream Deck part answers with the right ID). A parser must check the ID byte and reject such a report unless it has a documented reason to accept it.

### Per-lamp attributes (measured)

Source: `tools/windows/lamparray-lamps.ps1`, 2026-10-18. For each lamp, a LampAttributesRequest (a feature report SET that only selects the lamp ID) then a LampAttributesResponse (a feature report GET). No update or control report was sent. Raw data: [`lamps-2b0c.csv`](lamps-2b0c.csv) (108 lamps) and [`lamps-2b18.csv`](lamps-2b18.csv) (2 lamps), positions in micrometres.

Common to all 110 lamps: purposes = 1 (control), 8-bit red, green and blue (255 levels each), one intensity level (no separate brightness channel), no input binding. The keyboard's lamps are programmable (flag 1), answer with a 3 ms latency and all have z = 0.

Keyboard part `2b0c`, the 108 lamps grouped by Y position:

| Y (mm) | Lamps | IDs | Reading |
|---|---|---|---|
| 45 | 16 | 0–10, 66–70 | top row: fits Esc, F1–F12, then three keys |
| 65 | 17 | 11–21, 73–78 | number row plus three keys on the right |
| 85 | 17 | 22–32, 80–85 | |
| 105 | 14 | 33–43, 88–90 | no keys in the right-hand block |
| 125 | 15 | 44–54, 71, 91–93 | ID 71 (X = 365 mm) stands alone on the right |
| 145 | 15 | 55–65, 72, 79, 86, 87 | bottom row; IDs 59–61 share one position (the space bar) |
| 0 | 14 | 94–107 | **not keys**: 7 distinct X positions (77, 90, 185, 198, 238, 289, 345 mm), two lamps each, all at Y = 0 |

IDs 0–93 fit a full-size layout without a numpad (the Stream Deck module takes its place). The row counts are an inference from positions, not from key names: the interface gives no key labels. What lamps 94–107 are (ambient bar, indicators, ...) is **not known**, and their Y = 0 may be a placeholder rather than a measurement.

Stream Deck part `2b18`: two lamps at X = 386.0 mm and 418.3 mm, Y = 19.0 mm, z = 0.994 mm, 20 ms latency. Confirmed by eye (OpenRGB, "Dynamic Light" device, whole zone set to one colour, 2026-10-18): they are the LEDs around the two dials. Which lamp is the left dial and which the right has not been tested yet; by position, ID 0 (X = 386 mm) should be the left one.

Quirk: the keyboard part answers with an ID byte other than the one asked for. Attributes report (asked for 1): ID byte `04` on one run. Lamp responses (asked for 3): ID byte `02` for all 108. The Stream Deck part always answers with the right ID (224, 226). A guess, not verified: the keyboard echoes the last report ID used on the interface. Either way, the parser must check the ID byte.

Lamps 94–107 (visual test, OpenRGB, 2026-10-18): the user selected the last entries of the keyboard's LED list, applied white, and **nothing lit up** on the keyboard. Which exact entries were tried was not recorded, so this is not yet a full test of all 14. Working hypothesis: placeholders without a visible LED. Do not rely on them until each pair has been tested.

Not known yet: whether the LCD keys' backlight is a lamp (it does not seem to be: no lamp sits in the module's key area), which dial each Stream Deck lamp drives, and what lamps 94–107 are if they are real.

What this suggests, not yet verified:
- RGB may also be reachable through the standard HID LampArray interface (MI_04 and MI_03), whose report layout is public in the HID Usage Tables, instead of only the proprietary Bragi interface. No LampArray write has been tried.
- The rule against writing RGB reports before the format is understood still applies.

## Linux behaviour (measured)

Source: Kubuntu, kernel 7.0.0-38, 2026-10-18, keyboard firmware v2.3.88.

- The kernel's `usbhid` driver **does not bind the LampArray interfaces**: `usbhid 1-10.1:1.4: couldn't find an input interrupt endpoint` (keyboard, interface 4) and the same for `1-10.2:1.3` (Stream Deck module, interface 3). These interfaces only carry feature reports, so they have no interrupt endpoint.
- Consequence: no `/dev/hidraw*` node exists for them. The 4 keyboard nodes and 3 Stream Deck nodes expose no usage page 0x59 descriptor. Any tool reading LampArray through hidraw (OpenRGB's `HIDLampArrayController`, built at commit 96e198a) sees nothing on Linux, although it works on Windows.
- The interfaces are reachable by libusb with a control transfer (`GET_REPORT`, feature, `wIndex` = interface). Read-only probe: `tools/linux/lamparray-probe.c`. It reproduces the Windows attributes exactly: keyboard 108 lamps, 440 × 150 × 25 mm, kind 1, 3 ms; Stream Deck module 2 lamps, 20 ms. The keyboard answers report 1 with ID byte `04`, as on Windows.
- Access to `/dev/bus/usb/...` needs the `uaccess` rule in `tools/udev/70-galleon.rules`.

What this suggests, not yet verified: the RGB driver must use libusb control transfers on the LampArray interfaces (or the Bragi interface), not hidraw.

### LampArray write test (Linux, 2026-10-18)

Source: `tools/linux/lamparray-test-lamp.c`, run by the user on Kubuntu, keyboard firmware v2.3.88. Report layouts come from the interface's own descriptor (Stream Deck part, interface 3):

- 229: LampArrayControlReport, one 8-bit field, AutonomousMode (0 = host controls the lamps, 1 = the keyboard's own lighting).
- 227: LampMultiUpdateReport, 51 bytes with the ID: lamp count (8 bits), flags (8 bits), 8 lamp IDs (16 bits each), then 8 x (red, green, blue, intensity).

Sequence tested: control 229 with 0, then update 227 with one lamp (ID 0, red 255, intensity 255, flag 1 = update complete), 6 s later control 229 with 1. **Result: lamp 0 of the Stream Deck part lit red for 6 s, then the lighting went back to the keyboard's. No freeze.** Nothing is written to flash by these reports (standard LampArray, volatile).

Still open: which dial lamp 0 is (not recorded) and the effect of intensity values below 255.

### What LampArray drives, and what it does not (Linux, 2026-10-18)

Same firmware (keyboard v2.3.88), same sequence on both parts, with `tools/linux/lamparray-test-lamp` (single lamps, then full frames of all lamps in batches of 8 with the complete flag on the last batch, paced at the device's minimum interval) and with the OpenRGB plugin (all LEDs set to one colour):

- **Stream Deck part, interface 3 (2 lamps): works.** The rings around the dials change colour, one lamp per ring, in the tool and in OpenRGB.
- **Keyboard part, interface 4 (108 lamps): accepted but nothing visible.** Every report is acknowledged (`ok`, no error), yet no key changes colour, with the tool and with OpenRGB. Reading the 108 lamps works.
- **The 12 LCD keys' backlight: not affected** by either interface.

Second test, software mode held: `bragi-probe --hold 100` kept the Bragi render mode (property 3) at 2 while `lamparray-test-lamp keyboard sweep` ran in the same shell (both overlapped: lamps 0 to 107 lit in turn, full frames, control report 6 = 0 first). **Still no key changed.** So switching the keyboard out of its hardware lighting mode is not what keeps the keys from obeying LampArray. The Windows session had suspected Windows Dynamic Lighting rewriting the keys; Linux has no such service and shows the same result, so that is not the cause either.

So on this firmware the keys' RGB is not reachable through LampArray. Not tested: other firmware versions, and the vendor interface (usage page 0xFF42, "Bragi"/V2) that galleon-deck uses, which is the risky one (see the freeze warning above).



## Vendor interface seen from the Corsair Web Hub (usbmon capture, 2026-10-18)

Source: a usbmon capture (`dumpcap -i usbmon1`, 12294 packets, 71 s) on Linux while `corsair.com/web-hub` (Chrome, WebHID) connected to the keyboard and set a fixed colour three times (magenta, then red, with a colour change in between). The raw capture is NOT in the repository: it holds device identifiers (serial-like strings in the answers to command 0x08) and the bus carried the keyboard's keystrokes. Only the vendor interface was read (keyboard part, endpoints OUT 0x04 and IN 0x84, 324 request/answer pairs of exactly 128 bytes); the keystroke endpoint was never opened.

**Observed frame format** (the interface uses unnumbered 128-byte reports, so these are the 128 bytes written, with no report number):

- request: `00 01 00 <command> <arguments...>`
- answer: `00 00 00 <command> <status or data...>`

This differs from the `08 02 <property>` request given by galleon-deck and by OpenRGB's Corsair V2 controller: with that format the keyboard did not answer at all on this firmware (read-only probe `tools/linux/bragi-probe.c`, five properties, no reply in 500 ms).

**Commands seen** (counts in the capture): 0x01 (26), 0x02 (65), 0x05 (66), 0x06 (12), 0x07 (3), 0x08 (43), 0x09 (42), 0x0d (55), 0x12 (12). They match the command numbers of OpenRGB's `corsair_v2_cmd` (SET 0x01, GET 0x02, STOP_TX 0x05, BLK_W1 0x06, BLK_WN 0x07, START_TX 0x0D); that is an observation of the same numbers, not proof of the same semantics.

- `00 01 00 02 <property>`: read a property. Answers seen include property 0x02 = 0x03e8 (1000), 0x41 = 2, and 0x13 = bytes `02 03 58`, which reads as firmware **2.3.88**, the version the keyboard was updated to after the Windows measurements. The galleon-deck notes mention firmware 3.70.1, so the protocol details they give may belong to another firmware generation.
- `00 01 00 01 03 00 <1|2>`: set property 3 to 1 or 2 (what OpenRGB calls the render mode); the Web Hub toggles it many times, always answered `00 00 00 01 00`.
- `0x0d`, `0x09`, `0x08`, `0x05`: open a resource, query its size, read it, close it (a file-like sequence; interpretation, not documented).
- `0x06` / `0x07`: block writes. The three colour changes each produced one write whose payload contains the text "Profile": the Web Hub stored the colour in the keyboard's **saved profile**. That is a persistent write: **do not replay it** (project rule: nothing that writes to flash).

A second capture (2026-10-18, 60 s: rainbow effect, one setting change, then a fixed green) gives the same picture: 94 requests, only two block-write sequences, each one a write of the saved profile (the effect and then the green), and no stream of frames. So the Web Hub never uses a live software-lighting stream for these effects.

**Read-only access confirmed on Linux (2026-10-18)**, with `tools/linux/bragi-probe.c` through `/dev/hidraw` of the vendor interface (usage page 0xFF42, usage 1): the frame above is answered. Properties read, as `status=0 value`: 0x01 = 4, 0x02 = 1000 (0x3e8), 0x0a = 5, 0x13 = 0x580302 (firmware 2.3.88: bytes major 2, minor 3, patch 0x0058 = 88), 0x41 = 2, 0x6e = 1; property 0x05 asked without the extra argument 01 answers `status=5` (the Web Hub asks it with 01 and gets 0x0501).

Pitfall that cost three runs: on hidraw, the first byte of `write()` is the report number and the kernel drops it when it is 0. This interface has unnumbered 128-byte reports, so **129 bytes must be written** (a 0, then the 128-byte frame). Writing 128 bytes loses the first byte of the frame (usbmon showed 127-byte transfers and the keyboard stayed silent). Do not read from the same hidraw while a browser using WebHID is open: every reader gets a copy of every answer.

**Mode switch tested on Linux (2026-10-18)**: `bragi-probe --software-mode` sent `00 01 00 01 03 00 02` (property 3 = 2, software RGB mode in OpenRGB's naming), read the properties, then `00 01 00 01 03 00 01` (back to 1). Both were answered `00 00 00 01 00` (accepted), and the property reads gave the same values in both modes. No colour was sent. The visual effect on the keys during mode 2 was not recorded.

**The Web Hub's lighting record names 125 keys.** The profile write that sets a fixed colour is a resource of 0x89 (137) bytes. After a short header (`7e 20 01 00 00 00 01`), four colour bytes and the count `7d` (125), it lists 125 one-byte key identifiers, in this order (hex):
`04..45` (all 66 values), `47..65` (31 values), `69..6f`, `73..77`, `7a`, `7c`, `81..87`, `8b..91`.
That is HID keyboard usage numbers (04 = A, 1e = 1, 29 = Esc, ...), with 0x46 left out. So the keyboard's own lighting engine addresses **125 keys by usage number**, not 108 lamps by index as the LampArray interface does; how the two numberings relate is not known. The four colour bytes read as `ff, blue, green, red`: red was `ff 00 00 ff`, green `ff 00 ff 00`, magenta-ish `ff 4f 00 ff` (observation from three captures, not a documented layout). Block writes of this record are persistent profile writes: never replay them.

What this does not show: the live, non-persistent per-key lighting stream (software mode). The Web Hub does not use it for a fixed colour or for effects. iCUE does not detect this keyboard (new Corsair devices are managed by the Web Hub), so there is no known application whose traffic would show such a stream; the freeze warning above still applies to any attempt to write one.

## Web Hub capture and Linux hand-over

[`webhub-lighting-analysis.md`](webhub-lighting-analysis.md) gathers what was observed with Corsair Web Hub (vendor-interface traffic, where the static colour is, what is a profile write to flash and must never be replayed), the safe LampArray route, the Stream Deck facts, the open questions and how to reproduce the capture on Linux. The sanitized frames are in [`webhub-capture-2026-10-18.txt`](webhub-capture-2026-10-18.txt).

## Protocol facts read from the Web Hub's own client (2026-10-18)

Source: the public JavaScript of `corsair.com/web-hub` (main bundle `assets/BDiHBs-c.js`, read offline on 2026-10-18; only facts are recorded here, no code is copied, and the files are not in the repository). It names the keyboard's product ID 11020 (0x2b0c) and builds the frames seen in the usbmon captures, so it explains them.

**Frame header, 4 bytes** (the client calls it protocol header v1.5; the Galleon's frames in the captures have exactly this shape): byte 0 = 0, byte 1 = 1 (request), byte 2 = **session ID** (0 when no session is open), byte 3 = command. The frame is then zero-padded to the report length. Answers echo the command at byte 3; in an answer, byte 1 = 15 means "session expired" and byte 4 = 3 means "invalid state" (client's validation).

**Commands, as named by the client** (decimal in the client, hex here), all matching the numbers seen in the captures:

| Hex | Client's name | Arguments (as built by the client) |
|---|---|---|
| 01 | set property | property ID (2 bytes LE), value (4 bytes) |
| 02 | get property | property ID (2 bytes LE) |
| 03 | get several properties | count, then the IDs (2 bytes LE each) |
| 05 | unbind buffer | count, then buffer indexes |
| 06 | write buffer, begin | buffer index, total length (4 bytes LE), first data |
| 07 | write buffer, continue | buffer index, data |
| 08 | read buffer | buffer index |
| 09 | describe buffer | buffer index |
| 0a | calibration control | 2 bytes |
| 0b | create file | file ID (2 bytes LE) |
| 0c | delete file | file ID (2 bytes LE) |
| 0d | open file | buffer index, file ID (2 bytes LE) |
| 0f | **factory reset** | 1 byte. **Never send.** |
| 12 | ping (also used for pairing) | none; the Web Hub pings every ~30 s |
| 1b | host software session | start: `01`, 4 random bytes, `00`, answer byte 9 = session ID; stop: `02`, 4 zero bytes, session ID |

So the device exposes a small file system: a file is opened into a numbered buffer, then described (size), read or written, then unbound. The profile written by the Web Hub is such a file (file IDs of the form 0x6dNN seen in the captures). `0d 00 22` from galleon-deck would be "open file 0x22 into buffer 0"; what file 0x22 is on this keyboard is not known.

**Operating mode** (property 3, which OpenRGB names "render mode"): the client's names are 1 = SELF_OPERATED, 2 = HOST_CONTROLLED, **3 = BOOTLOADER**, 4 = SLEEP, **5 = MCU_BOOTLOADER**, 6 = LIMITED_HOST_CONTROLLER, 7 = ESPORTS. Only 1 and 2 may ever be written by our tools; 3 and 5 would put the device in a bootloader. `bragi-probe` writes only 1 and 2.

**Files read on Linux (2026-10-18, `tools/linux/bragi-read-file.c`, read-only: open, describe, read, unbind)**, keyboard firmware 2.3.88, hardware mode, no session:

- File 0x000f: opens, size 8, content `13 00 02 00 60 6d 65 6d`, identical to what the Web Hub read in the capture. This validates the tool and the frame layout.
- File **0x0022** (the file galleon-deck's `0d 00 22` opens and OpenRGB calls LIGHT_CTRL2): it **exists**, opens with status 0, size 8, content **all zeros** while idle. What it controls is not known.
- Reply layout seen: byte 4 = status, data from byte 5; the describe reply carries the size as 16 bits at byte 7 (0x23 for file 0x26, 0x89 for the profile, 8 here).

Not found in the client: any name or function for a live per-key colour stream. The client has lighting "layers" and "effects" stored as profile data.

## Captures

Put raw captures under `captures/`, named `YYYY-MM-DD-<what>.pcapng`, with a short note on what was done during the capture.
