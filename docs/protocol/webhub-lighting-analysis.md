# Corsair Web Hub traffic, and everything known to drive the Galleon on Linux

Written for whoever implements the Linux side (Claude Code or a person). It separates what was **observed** (measured on the hardware, with the file or script that shows it) from what is **inferred** (reasoned, not tested) and from what is **unknown**. Raw captures are never committed: they hold device serial numbers and possibly keystrokes (`captures/*.pcapng` is git-ignored).

Hardware and software at the time: Galleon 100 SD keyboard firmware **2.3.88** (it was 1.4.73 until that day; the LampArray data was identical before and after), Corsair Web Hub **v1.2.33**, Windows 11.

## 1. Summary

| Goal | Safe route | State |
|---|---|---|
| Dials' LEDs (2 lamps, USB part `2b18`) | HID LampArray, feature reports | **Works**: OpenRGB lit them on Windows |
| Keyboard keys (108 lamps, USB part `2b0c`) | HID LampArray, feature reports | Descriptor fully read; colours **not obeyed** in OpenRGB on Windows, cause unknown (see section 6) |
| Keyboard keys, Corsair "Bragi" vendor protocol | Vendor interface MI_01 | Only Web Hub's **profile writes to onboard storage** were captured: **never replay them** |
| Stream Deck module (keys, screen, dials) | Interface MI_00 of `2b18`, from galleon-deck | Facts sourced, none tested here |

Rule of the project: **never write to the keyboard's flash** (hardware profile, persistent mode).

## 2. What Web Hub sends (observed)

Source: [`webhub-capture-2026-10-18.txt`](webhub-capture-2026-10-18.txt), a sanitized extract of a 90 s USBPcap capture while four lighting changes were made in Web Hub's "Static" layer.

- Keyboard `1b1c:2b0c`, vendor interface MI_01 (usage page `0xFF42`, 128-byte reports), endpoints `0x04` OUT and `0x84` IN, interrupt transfers of 128 bytes. No other interface of the keyboard and nothing of the Stream Deck part carried Web Hub traffic.
- Frames to the keyboard start with `00 01 00`, then a command byte, then arguments, zero-padded to 128 bytes. Replies start with `00 00 00` and echo the command byte, followed by data.
- A poll every ~2.4 s: `00 01 00 02 e1` answered by `00 00 00 02`.
- Each lighting change is one **burst of 29 frames out and about 29 back**, about 1 s long. There were exactly four bursts, at about 12 s, 30 s, 50 s and 68 s, matching the four changes (two static colours, an animated effect, static again). Nothing else was sent between them: no per-frame colour stream.

### Commands seen

The command bytes match the names OpenRGB gives to the Corsair "V2" (Bragi) protocol in `Controllers/CorsairPeripheralV2Controller/CorsairPeripheralV2Controller.h` (GPL-2.0-or-later). The names below are OpenRGB's, applied here by analogy: **inferred**, not confirmed for the Galleon.

| Byte | OpenRGB name | Seen in the capture |
|---|---|---|
| `01` | SET | `01 03 00 02` at the start of a burst, `01 03 00 01` at its end |
| `02` | GET | the poll `02 e1` |
| `05` | STOP_TX | `05 01`, `05 01 01`, `05 01 02`, `05 01 03` |
| `06` | BLK_W1 (block write, first packet) | `06 <handle> <length> …` |
| `07` | BLK_WN (block write, next packets) | continuation of the key list |
| `0d` | START_TX | `0d 00 65 6d`, `0d 01 6a 6d`, `0d 02 67 6d`, `0d 03 65 6d`, `0d 01 0f` |
| `08`, `09` | (no name there) | `09` answers with a small number (`00 00 00 32`, `00 00 00 08`); `08` answers with a record that contains the handle list `69 6d`, `67 6d`, `68 6d` and the text `Profile 1` |

Reading of a burst (inferred): switch the keyboard to a mode with `01 03 00 02`, then for each of several "handles" (`6d65`, `6d67`, `6d6a`, little-endian, the second byte `6d` is common) open a transaction, query a size, read the current record, write the new record with `06`/`07`, close the transaction with `05`; and finally `01 03 00 01`. The handles are profile storage nodes: **this is a write to the keyboard's persistent profile.** Do not replay it.

### Where the colour is (observed, partly inferred)

In the `06 01 89 …` write of each burst, only three bytes differ between the two static colours: offsets 17, 18, 19 of the frame (`4f 29 ff` for the first colour, `32 dd 5d` for the green). `32 dd 5d` is a green (50, 221, 93), so the order red, green, blue is likely; only two colours were tested, so this is a deduction. The byte at offset 16 is `ff` and the byte at offset 20 is `7d` in both. The `06 01 89` write is followed by `07 01 76 …` and a list of key identifiers (`04 05 06 … 75 …`, one per key): the layer applies to those keys.

One more byte changes at every burst without relation to the colour (offset 11 of the last `06` write: `61`, `73`, `87`, `99` for bursts A to D). Unidentified: a counter or a checksum. The animated-effect burst differs strongly (119 bytes in the main write): its layout is not decoded.

## 3. The Bragi software-lighting sequence (from galleon-deck, not tested)

galleon-deck's `docs/PROTOCOL.md` (MIT) describes a software lighting attempt on the same vendor interface, **marked unsafe** by its author:

- enable `08 01 03 00 02`, start a transaction `0d 00 22`, colour data in `06`/`07` block writes, stop `05 01 00`;
- the only working write was white, as a planar buffer of 3 × 138 bytes; other layouts froze the whole keyboard (typing included) until unplugged, and a 193-LED buffer crashed it.

The Galleon's frames in this capture start with `00 01 00`, while galleon-deck writes `08 …`: the two dialects differ in the framing, or the leading bytes are handled differently by Windows and by hidraw. The command numbers (`01`, `05`, `06`, `07`, `0d`) agree. The value `0x22` in `0d 00 22` is what OpenRGB calls `CORSAIR_V2_LIGHT_CTRL2` and sends in its START_TX packet for lighting on other Corsair keyboards. No capture of a software lighting stream exists: Web Hub does not produce one (it saves profiles), and iCUE does not list this keyboard. The galleon-deck sequence was confirmed by hand on firmware 2.3.88 (Windows): see [Live per-key lighting](README.md#live-per-key-lighting-windows-2026-10-25).

## 4. HID LampArray (the safe route), measured

Both USB parts expose the standard HID LampArray (usage page `0x59`); see the tables above in this folder:

- keyboard `2b0c`, interface MI_04: attributes report ID 1, lamp attributes request 2 and response 3, multi-update 4, range update 5, control 6; 108 lamps (IDs 0–93 are keys in a no-numpad layout, IDs 94–107 are 14 lamps at Y = 0 that are not keys; probably the ambient bar, whose 7 segments Web Hub draws above the F-row); 3 ms minimum update interval; data in [`lamps-2b0c.csv`](lamps-2b0c.csv);
- Stream Deck part `2b18`, interface MI_03: the same reports with IDs 224–229; 2 lamps (the two dials' rings); 20 ms; data in [`lamps-2b18.csv`](lamps-2b18.csv).
- The multi-update report is 51 bytes with its ID: 8-bit lamp count, 8-bit flags, eight 16-bit lamp IDs, then eight groups of four 8-bit values (red, green, blue, intensity). Layout read from the report descriptor; no write was sent by us.
- Quirk: the keyboard answers feature reads with an ID byte other than the requested one (`04` for report 1, `02` for report 3). The parser must check the ID byte and not trust the reply blindly.

Linux reading and writing go through `hidraw` with `HIDIOCGFEATURE` and `HIDIOCSFEATURE`; the scripts `tools/windows/lamparray-attributes.ps1` and `lamparray-lamps.ps1` show the exact request sequence (select a lamp with report 2, then read report 3).

## 5. Stream Deck module

From galleon-deck's `docs/PROTOCOL.md` (MIT), interface 0 (MI_00): keep-alive feature report `03 27` every 500 ms (the module ignores everything else until it arrives, and falls back to standalone mode when it stops); top screen 720 × 384; 4 × 3 grid of 160 × 160 JPEG keys; 1024-byte output reports with a header (`02 07 <key> <last> <len> <part>` for keys, `02 0c …` for the screen); input reports `01 00 0c 00` followed by 12 key states, and dial reports `01 03 03 00 …`. This agrees with the sizes measured here for MI_00: input 512, output 1024, feature 32 bytes. None of it was tested on this unit, and the module is configured by Elgato's Stream Deck application on Windows, not by Web Hub.

## 6. Open questions

1. Why the keyboard's 108 lamps do not obey OpenRGB's LampArray writes while the dials do. Suspects: Windows Dynamic Lighting (solid colour effect, on, and it lists only the keyboard) rewriting the keys; the keyboard's hardware profile. The decisive test (Windows setting off, OpenRGB freshly started, one key set to a vivid colour) was not run. On Linux there is no Windows lighting service, so the same test is clean there.
2. What lamps 94–107 are. One visual test in OpenRGB lit nothing, but it was not exhaustive.
3. The colour layout of a **software** lighting stream (planar or interleaved, packet framing). Needs a capture of an application that streams colours; none was available.
4. Which dial each of the two lamps is (by position, lamp 0 at X = 386 mm should be the left one).
5. The meaning of the unidentified counter byte and of commands `08` and `09`.

## 7. How to reproduce a capture on Linux

1. `sudo cp tools/udev/70-galleon.rules /etc/udev/rules.d/` then `sudo udevadm control --reload && sudo udevadm trigger`, and replug the keyboard.
2. In a Chromium-family browser installed as a package (not Flatpak or Snap, which may block `hidraw`), open Corsair Web Hub and connect the keyboard.
3. `sudo modprobe usbmon`, find the bus with `lsusb`, capture that bus in Wireshark. Use only the mouse during the capture.
4. Strip the serial number before sharing anything, and never commit the raw file.
