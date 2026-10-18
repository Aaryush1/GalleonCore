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

**Firmware at the time of all measurements below: keyboard v1.4.73** (as shown by the official update tool, which offered v2.3.88 on 2026-10-18). A major version change may alter interfaces, report IDs and lighting behaviour: measurements made after the update must be recorded separately, with the new version.

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

Source: Kubuntu, kernel 7.0.0-38, 2026-10-18, keyboard firmware v1.4.73.

- The kernel's `usbhid` driver **does not bind the LampArray interfaces**: `usbhid 1-10.1:1.4: couldn't find an input interrupt endpoint` (keyboard, interface 4) and the same for `1-10.2:1.3` (Stream Deck module, interface 3). These interfaces only carry feature reports, so they have no interrupt endpoint.
- Consequence: no `/dev/hidraw*` node exists for them. The 4 keyboard nodes and 3 Stream Deck nodes expose no usage page 0x59 descriptor. Any tool reading LampArray through hidraw (OpenRGB's `HIDLampArrayController`, built at commit 96e198a) sees nothing on Linux, although it works on Windows.
- The interfaces are reachable by libusb with a control transfer (`GET_REPORT`, feature, `wIndex` = interface). Read-only probe: `tools/linux/lamparray-probe.c`. It reproduces the Windows attributes exactly: keyboard 108 lamps, 440 × 150 × 25 mm, kind 1, 3 ms; Stream Deck module 2 lamps, 20 ms. The keyboard answers report 1 with ID byte `04`, as on Windows.
- Access to `/dev/bus/usb/...` needs the `uaccess` rule in `tools/udev/70-galleon.rules`.

What this suggests, not yet verified: the RGB driver must use libusb control transfers on the LampArray interfaces (or the Bragi interface), not hidraw.

### LampArray write test (Linux, 2026-10-18)

Source: `tools/linux/lamparray-test-lamp.c`, run by the user on Kubuntu, keyboard firmware v1.4.73. Report layouts come from the interface's own descriptor (Stream Deck part, interface 3):

- 229: LampArrayControlReport, one 8-bit field, AutonomousMode (0 = host controls the lamps, 1 = the keyboard's own lighting).
- 227: LampMultiUpdateReport, 51 bytes with the ID: lamp count (8 bits), flags (8 bits), 8 lamp IDs (16 bits each), then 8 x (red, green, blue, intensity).

Sequence tested: control 229 with 0, then update 227 with one lamp (ID 0, red 255, intensity 255, flag 1 = update complete), 6 s later control 229 with 1. **Result: lamp 0 of the Stream Deck part lit red for 6 s, then the lighting went back to the keyboard's. No freeze.** Nothing is written to flash by these reports (standard LampArray, volatile).

Still open: which dial lamp 0 is (not recorded) and the effect of intensity values below 255.

### What LampArray drives, and what it does not (Linux, 2026-10-18)

Same firmware (keyboard v1.4.73), same sequence on both parts, with `tools/linux/lamparray-test-lamp` (single lamps, then full frames of all lamps in batches of 8 with the complete flag on the last batch, paced at the device's minimum interval) and with the OpenRGB plugin (all LEDs set to one colour):

- **Stream Deck part, interface 3 (2 lamps): works.** The rings around the dials change colour, one lamp per ring, in the tool and in OpenRGB.
- **Keyboard part, interface 4 (108 lamps): accepted but nothing visible.** Every report is acknowledged (`ok`, no error), yet no key changes colour, with the tool and with OpenRGB. Reading the 108 lamps works.
- **The 12 LCD keys' backlight: not affected** by either interface.

So on this firmware the keys' RGB is not reachable through LampArray. Not tested: firmware v2.3.88 (offered by the official tool; it would need its own recorded measurements), and the vendor interface (usage page 0xFF42, "Bragi"/V2) that galleon-deck uses, which is the risky one (see the freeze warning above).



## Vendor interface seen from the Corsair Web Hub (usbmon capture, 2026-10-18)

Source: a usbmon capture (`dumpcap -i usbmon1`, 12294 packets, 71 s) on Linux while `corsair.com/web-hub` (Chrome, WebHID) connected to the keyboard and set a fixed colour three times (magenta, then red, with a colour change in between). The raw capture is NOT in the repository: it holds device identifiers (serial-like strings in the answers to command 0x08) and the bus carried the keyboard's keystrokes. Only the vendor interface was read (keyboard part, endpoints OUT 0x04 and IN 0x84, 324 request/answer pairs of exactly 128 bytes); the keystroke endpoint was never opened.

**Observed frame format** (the interface uses unnumbered 128-byte reports, so these are the 128 bytes written, with no report number):

- request: `00 01 00 <command> <arguments...>`
- answer: `00 00 00 <command> <status or data...>`

This differs from the `08 02 <property>` request given by galleon-deck and by OpenRGB's Corsair V2 controller: with that format the keyboard did not answer at all on this firmware (read-only probe `tools/linux/bragi-probe.c`, five properties, no reply in 500 ms).

**Commands seen** (counts in the capture): 0x01 (26), 0x02 (65), 0x05 (66), 0x06 (12), 0x07 (3), 0x08 (43), 0x09 (42), 0x0d (55), 0x12 (12). They match the command numbers of OpenRGB's `corsair_v2_cmd` (SET 0x01, GET 0x02, STOP_TX 0x05, BLK_W1 0x06, BLK_WN 0x07, START_TX 0x0D); that is an observation of the same numbers, not proof of the same semantics.

- `00 01 00 02 <property>`: read a property. Answers seen include property 0x02 = 0x03e8 (1000), 0x41 = 2, and 0x13 = bytes `02 03 58`, which reads as firmware **2.3.88**, the version the official tool offered (the Windows measurements were taken on v1.4.73): whether the keyboard was updated in between is not known.
- `00 01 00 01 03 00 <1|2>`: set property 3 to 1 or 2 (what OpenRGB calls the render mode); the Web Hub toggles it many times, always answered `00 00 00 01 00`.
- `0x0d`, `0x09`, `0x08`, `0x05`: open a resource, query its size, read it, close it (a file-like sequence; interpretation, not documented).
- `0x06` / `0x07`: block writes. The three colour changes each produced one write whose payload contains the text "Profile": the Web Hub stored the colour in the keyboard's **saved profile**. That is a persistent write: **do not replay it** (project rule: nothing that writes to flash).

What this does not show: the live, non-persistent per-key lighting stream (software mode). The Web Hub does not use it for a fixed colour. A capture of iCUE running a software effect would be needed, and the freeze warning above still applies.

## Captures

Put raw captures under `captures/`, named `YYYY-MM-DD-<what>.pcapng`, with a short note on what was done during the capture.
