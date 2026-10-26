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

Stream Deck part `2b18`: two lamps at X = 386.0 mm and 418.3 mm, Y = 19.0 mm, z = 0.994 mm, 20 ms latency. Confirmed by eye (OpenRGB, "Dynamic Light" device, whole zone set to one colour, 2026-10-18): they are the LEDs around the two dials. By position, ID 0 (X = 386 mm) should be the left one, but through Windows Dynamic Lighting it is the other way round: with `Windows.Devices.Lights.LampArray` on this part, index 1 lights the left dial and index 0 the right one (confirmed by eye, 2026-10-26, firmware 2.3.88). Whether Windows' indices are the HID lamp IDs here was not checked. Windows gives a packaged app control of the dials about 5 s after it starts, and only when it is the background lighting controller in Settings.

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

What this does not show: the live, non-persistent per-key lighting stream (software mode). The Web Hub does not use it for a fixed colour or for effects. iCUE does not detect this keyboard (new Corsair devices are managed by the Web Hub), so there is no known application whose traffic would show such a stream; the freeze warning above still applies to any attempt to write one. A working stream was later written by hand on Windows: see [Live per-key lighting](#live-per-key-lighting-windows-2026-10-25).

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

So the device exposes a small file system: a file is opened into a numbered buffer, then described (size), read or written, then unbound. The profile written by the Web Hub is such a file (file IDs of the form 0x6dNN seen in the captures). `0d 00 22` from galleon-deck is "open file 0x22 into buffer 0"; file 0x22 holds the live key colours in host-controlled mode (see below).

**Operating mode** (property 3, which OpenRGB names "render mode"): the client's names are 1 = SELF_OPERATED, 2 = HOST_CONTROLLED, **3 = BOOTLOADER**, 4 = SLEEP, **5 = MCU_BOOTLOADER**, 6 = LIMITED_HOST_CONTROLLER, 7 = ESPORTS. Only 1 and 2 may ever be written by our tools; 3 and 5 would put the device in a bootloader. `bragi-probe` writes only 1 and 2.

**Files read on Linux (2026-10-18, `tools/linux/bragi-read-file.c`, read-only: open, describe, read, unbind)**, keyboard firmware 2.3.88, hardware mode, no session:

- File 0x000f: opens, size 8, content `13 00 02 00 60 6d 65 6d`, identical to what the Web Hub read in the capture. This validates the tool and the frame layout.
- File **0x0022** (the file galleon-deck's `0d 00 22` opens and OpenRGB calls LIGHT_CTRL2): it **exists**, opens with status 0, size 8, content **all zeros** while idle. In host-controlled mode, writing to a buffer bound to it sets the live key colours (see [Live per-key lighting](#live-per-key-lighting-windows-2026-10-25)).
- Reply layout seen: byte 4 = status, data from byte 5; the describe reply carries the size as 16 bits at byte 7 (0x23 for file 0x26, 0x89 for the profile, 8 here).

Not found in the client: any name or function for a live per-key colour stream. The client has lighting "layers" and "effects" stored as profile data.

## Live per-key lighting (Windows, 2026-10-25)

> **Safety, read before writing a frame.**
> - **The keyboard stays in host-controlled mode after a crash.** If the writing process dies, the keys keep the last frame until something unbinds buffer 0 and sets property 3 back to 1. Plan for that: release on exit, on suspend and on session end, and run a separate watchdog that releases when the writer dies.
> - **Give way to the Web Hub and any other controller.** Two writers make the keys flicker, and the keyboard reports no error to either of them. Stop sending as soon as another host shows up.
> - **Write only property 3 = 1 or 2.** Values 3 and 5 put the device in a bootloader. Never create, write or delete files other than the live frame in buffer 0.
> - Earlier attempts with other frame layouts froze the whole keyboard until it was unplugged (see [galleon-deck](webhub-lighting-analysis.md)). Use exactly the layout below.

Measured by an outside contributor on one Galleon 100 SD, firmware 2.3.88, Windows 11, through the vendor interface (usage page 0xFF42, usage 1, 128-byte reports). The tool is a small C# library that sends only allowlisted commands, modeled on `bragi-probe`. Source and run notes: [Aaryush1/GalleonCore `lab/`](https://github.com/Aaryush1/GalleonCore/tree/key-lighting/lab). Discussion: [#56](https://github.com/Paullux/GalleonCore/issues/56).

### Verified on the hardware

The owner watched every visual result on the keyboard. The counts come from the tool's command log.

**Sequence** (header as above, with the session ID):

1. Start a session (`1b`, `01`, 4 random bytes, `00`).
2. Set property 3 to 2 (host-controlled).
3. Open file 0x0022 into buffer 0 (`0d 00 22 00`).
4. Write one frame into buffer 0: `06` with the total length (4 bytes LE) and the first data, then `07` continuations. The frame is `12 00` followed by 146 RGB triplets, one per LED index (440 bytes in all).
5. To give the keys back: unbind buffer 0 (`05 01 00`), then set property 3 to 1. The keys return to the onboard profile.

**LED indices.**

- Index 0x3A lit F1. During a typing test that lit each pressed key by its USB HID usage, the key that lit was the key pressed.
- Index 0x81 (129) is the Copilot key, which matches the Web Hub key table.
- Indices from 0x82 drive the light strip along the top edge. The first few light single LEDs from the left end. Later indices light larger sections.
- Eighteen indices that are absent from the Web Hub lighting record (`00..03`, `66..68`, `70..72`, `78`, `79`, `7b`, `7d..80`, `88`) lit nothing.

**Release.**

- After the host process was killed mid-stream, the keys kept the last frame and typing still worked. A new session that unbound buffer 0 and set mode 1 gave the keys back, with no replug. A watchdog process doing this released the keys 0.33 to 0.38 s after a deliberate crash.
- A USB replug returns the keyboard to mode 1.
- Releasing on Windows suspend works. The keyboard wakes into its onboard profile.

**Another host connecting.** When the Web Hub connects, it sets mode 1. Frame writes keep being answered with status 0. Answers to the Web Hub's own polling (about every 2.5 s) arrive on the same input. If both write, the keys flicker between the two. The contributor's tool treats any answer it did not ask for, or a mode other than 2, as another host, and stops sending until 60 s of quiet. With that rule the Web Hub showed the onboard colours with no flicker.

**Rate and typing.**

- 20 frames per second for 10 minutes: 0 failed writes, 0 timeouts. The owner typed fast during the run and saw no lag and no missed keys.
- One frame (four reports, each waiting for its answer) takes about 28 ms.

### Worked example: one frame, F1 green

Report length 129 bytes on Windows (report ID 0, then 128 bytes). Bytes below are the 128 after the report ID, as in the log that follows. Session ID 1. The frame is `12 00` and 146 RGB triplets (440 bytes), all black except LED 0x3A (F1) green. It goes out as one `06` and three `07` reports:

| Report | Header | Payload | Frame bytes carried | LEDs |
|---|---|---|---|---|
| 1 | `00 01 01 06` | `00` (buffer 0), `b8 01 00 00` (length 440, LE32), then frame bytes | 0–118 (119 bytes): `12 00` + 39 triplets | 0x00–0x26 |
| 2 | `00 01 01 07` | `00` (buffer 0), then frame bytes | 119–241 (123 bytes) | 0x27–0x4F |
| 3 | `00 01 01 07` | `00`, then frame bytes | 242–364 (123 bytes) | 0x50–0x78 |
| 4 | `00 01 01 07` | `00`, then frame bytes | 365–439 (75 bytes) | 0x79–0x91 |

F1's green byte is frame byte 2 + 3 × 0x3A + 1 = 177. That falls in report 2 at byte 63 (4 header bytes, 1 buffer byte, then frame byte 119 onward). Every report is zero-padded to the full length. Each answer echoes the command at byte 3 with status 0 at byte 4.

### Command log

From the run that confirmed F1 (2026-10-25). Command reports and answers only: no input reports, keystrokes or device paths were logged on this interface. The 4 random bytes of the session start are replaced with `rr`. Trailing zero padding is cut, as in the tool's log. Lines starting with `#` are the tool's own notes.

```text
17:15:32.537 TX 00 01 00 1b 01 rr rr rr rr
17:15:32.543 RX 00 00 00 1b 00 rr rr rr rr 01
17:15:32.544 # session start: status 0, session id 1
17:15:32.545 TX 00 01 01 02 03 00 00 00
17:15:32.551 RX 00 00 01 02 00 01 00 00
17:15:32.552 # operating mode (prop 3): status 0, value 1
17:15:32.554 TX 00 01 01 01 03 00 02 00
17:15:32.559 RX 00 00 01 01 00 00 00 00
17:15:32.559 # set mode 2: status 0
17:15:32.560 TX 00 01 01 0d 00 22 00 00
17:15:32.567 RX 00 00 01 0d 00 00 00 00
17:15:32.567 # open 0x22: status 0
17:15:32.567 # writing one frame: only LED 0x3A (F1) green, 440 bytes
17:15:32.569 TX 00 01 01 06 00 b8 01 00 00 12
17:15:32.575 RX 00 00 01 06 00 00 00 00
17:15:32.576 TX 00 01 01 07 00 [58 zero bytes] ff
17:15:32.583 RX 00 00 01 07 00 00 00 00
17:15:32.583 TX 00 01 01 07 00 00 00 00
17:15:32.591 RX 00 00 01 07 00 00 00 00
17:15:32.591 TX 00 01 01 07 00 00 00 00
17:15:32.599 RX 00 00 01 07 00 00 00 00
17:15:32.599 # FRAME SENT. Holding 10 s: what do the keys show?
17:15:33.612 TX 00 01 01 12 00 00 00 00
17:15:33.619 RX 00 00 01 12 00 00 00 00
             (9 more ping/answer pairs, one per second)
17:15:42.740 TX 00 01 01 05 01 00 00 00
17:15:42.747 RX 00 00 01 05 00 00 00 00
17:15:42.747 TX 00 01 01 01 03 00 01 00
17:15:42.963 RX 00 00 01 01 00 00 00 00
17:15:42.963 # set mode 1: status 0
17:15:42.963 TX 00 01 01 02 03 00 00 00
17:15:42.971 RX 00 00 01 02 00 01 00 00
17:15:42.971 # release: mode now 1 (status 0)
17:15:42.971 # RELEASED: keyboard back in self-operated mode.
```

The keyboard pings are the tool's own keep-alive while it holds the frame. The release is the unbind of buffer 0 (`05 01 00`), then mode 1, then a read of property 3 to confirm.

### Not verified (assumptions and open questions)

- **LED index = HID usage for every key.** Seen for F1, the Copilot key and the keys pressed in the typing test, not checked key by key across the board.
- **The strip map past the first few indices** is not finished: which index lights which section, and whether sections overlap.
- **The 20 fps cap** follows OpenRGB's limit for other Bragi keyboards. Faster rates were not tried, so the real limit is unknown.
- **Status 0 after the Web Hub takes over** is read here as "accepted and ignored". Nothing confirms what the keyboard does with those frames.
- **What file 0x0022 is.** It reads as 8 zero bytes while idle and accepts a 440-byte frame in host-controlled mode. Its real size and meaning are unknown.
- **Other firmware, other units, Linux.** Only firmware 2.3.88 on one keyboard under Windows was tested. The stream has not been tried over hidraw.
- **Other controllers.** Only the Web Hub was tested as the other host. iCUE does not list this keyboard, and Windows Dynamic Lighting on the keyboard's own LampArray was not tested alongside the stream.

## Stream Deck module facts (interface 0 of `1b1c:2b18`), checked 2026-10-18

Measured on this unit (read-only: the HID report descriptor and GET feature reports):
- Reports are numbered. Output ID 2 = 1023 data bytes (1024 with the ID), input ID 1 = 511 (512), feature IDs 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 20, 21 = 31 (32). So galleon-deck's `02 ...` and `03 27` are report ID then data.
- Feature report 8 (layout): `04 03 a0 00 a0 00 d0 02 00 05 02 05 00 00 04 00 d0 02 80 01 ...`. It holds 160 x 160 (`a0 00 a0 00`) and 720 x 384 (`d0 02 80 01`).
- Feature reports 4, 5, 7 carry firmware strings from byte 6: `2.00.002`, `3.06.005`, `3.06.005`.
- The module has four HID interfaces: 0 = control (the one to open), 1 = numpad keyboard, 2 = dial consumer keys, 3 = LampArray without endpoints.

Cross-checked against a second, independent implementation, OpenActionAPI/rust-elgato-streamdeck pull request 63 (linked from OpenDeck issue 272): VID/PID 1b1c:2b18, interface 0 only, keep-alive `03 27` in a 32-byte feature report every 500 ms, keys 160 x 160 JPEG and screen 720 x 384 JPEG, both without rotation or mirroring.

That implementation also lights the dial rings through the Stream Deck protocol: a 6-byte feature report `03 24 <led> <red> <green> <blue>`, 4 LEDs per dial (led = (1 - dial) * 4 + segment, so dial 0 uses LEDs 4 to 7 and dial 1 uses LEDs 0 to 3). `galleond` sends it for the themes (`dialRingReports`, padded to 32 bytes); the test on the module is pending. The LampArray interface sees only one lamp per ring.

Image headers (galleon-deck, and the Stream Deck Plus conventions the Rust code reuses for the Galleon): key `02 07 <key> <last> <len u16le> <part u16le>`, screen `02 0c <x> <y> <w> <h> <last> <part> <len> 00`.

**Confirmed on the hardware, 2026-10-18 (firmware module 3.06.005):** `galleond` drew a 720 x 384 JPEG on the top screen with this screen header (little-endian fields, `last` = 1 on the final report, `part` counting from 0, 1008 bytes of JPEG per report, report ID 2, 1024 bytes written through hidapi) and the module displayed it correctly (photo of the unit). The keep-alive `03 27` every 500 ms kept it in host mode. After `galleond` was stopped, the screen went back to the module's default image in less than a minute (observed by the user, not timed), so nothing stays latched: the firmware leaves host mode by itself when the keep-alive stops. **Key images confirmed on the hardware, 2026-10-19** (`galleond --test-keys`): the 160 x 160 JPEGs sent with the key header `02 07 <key> <last> <len u16le> <part u16le>` (up to 1016 bytes of JPEG per report) are drawn upright, unmirrored, filling the whole key, and key N (0 top left, row by row) shows the picture sent for N (photo of the unit).

**Input reports confirmed on the hardware, 2026-10-19** (`galleond --trace-input`, keep-alive running):

- Keys: `01 00 0c 00` then 12 bytes (1 = pressed). Key 0 is the top left key, then row by row to key 11 at the bottom right (a grid of 3 columns by 4 rows); each key gave one press and one release in order.
- Dial buttons: `01 03 03 00 00 <dial 0> <dial 1>` with 1 = pressed. Pressing the left dial gave `... 00 01 00`, the right dial `... 00 00 01`, so the left dial is dial 0.
- Dial rotation: `01 03 03 00 01 <dial 0> <dial 1>`, a signed byte per dial: one click clockwise gave `01` (+1), one click anticlockwise `ff` (-1).
- The reports are 512 bytes long on hidraw with the ID, zero-padded; only the first 7 (dials) or 16 (keys) bytes matter.

## Captures

Put raw captures under `captures/`, named `YYYY-MM-DD-<what>.pcapng`, with a short note on what was done during the capture.

## The module shows a picture only when a second transfer follows it (measured 2026-10-19)

Observed on the unit (firmware 3.06.005, Linux, hidapi), with two phone videos of the module filmed while `galleond --measure-display-lag` flipped the dial rings and the screen together (the ring command takes effect at once; the picture is what is late).

- A picture sent **once** (the normal way) stays invisible until the **next** picture is sent: the screen is always one picture behind. With one picture every 2.5 s, the screen showed picture N when picture N + 1 was being sent, about 2.5 s late.
- The same picture sent **twice in a row** is shown at once: its number appeared at the same moment as the ring changed colour (three flips, three times).
- A keep-alive report (`03 27`) sent right after the picture does **not** flush it: same one-picture lag as sending once.

What galleond does: `StreamDeck::setScreenImage` and `setKeyImage` send every picture twice (the same bytes), unless called with `flush = false` (pictures of a stream, such as the screensaver video: each one is shown when the next arrives). Why the module behaves this way is not known; the duplicate costs a second transfer (about 25 ms for the screen). Key images very probably behave the same (the key page of a theme only showed one press late); that is applied but not yet filmed.
