# G1 results: read-only probe (2026-10-24, 20:16–20:17 CDT)

Tool: `tools/Probe` (allowlist: session start, get property, ping, open/describe/read/unbind only). Logs in `logs/g1-*.log`, backup in `backups/20261024-201729/` (both gitignored). **No property was set and nothing was written.** The keyboard stayed in self-operated mode (property 3 = 1) throughout.

## Stage A: HID collections of 1b1c:2b0c

| Interface | Usage page / usage | In / out / feature bytes | Role |
|---|---|---|---|
| MI_00 | 0x0001/0x06 ×2, 0x000C/0x01 | 15–18 / 2 / 0 | standard keyboard + consumer keys |
| **MI_01** | **0xFF42 / 0x01** | **129 / 129 / 0** | **Bragi command channel: 128-byte reports + report ID 0** |
| MI_02 | 0xFF42 / 0x02 | 65 / 0 / 0 | Bragi notifications |
| MI_03 | 0x0001/0x02 | 7 / 0 / 0 | mouse-class collection |
| MI_04 | 0x0059 / 0x01 | 0 / 0 / 51 | Dynamic Lighting LampArray (acks, keys don't change) |

Settles the 64-vs-128 conflict: **128-byte reports**, as GalleonCore measured. Web Hub's 64 is only a default; it pads to the device's report length.

## Stage B: session and properties

- Session start (op 0x1B `[1, rnd4, 0]`, session byte 0): status 0, **session id 1** at reply byte 9. v1.5 framing `[0, 1, session, op]` confirmed.

| Prop | Value | Meaning |
|---|---|---|
| 3 | 1 | self-operated (hardware) mode |
| 261 | 0x0501 | Bragi protocol version (5.1?) |
| 19 | 0x00580302 | **firmware 2.3.88** (bytes 02 03 58). galleon-deck's lit-keys result was on 3.70.1. |
| 110 | 0 | active profile index 0 |
| 2 | 800 | brightness 80 % |
| 65 | 1 | ANSI layout |
| 62 | 8 380 416 | free storage bytes (~8 MB) |
| 17 / 18 | 0x1B1C / 0x2B0C | VID / PID |
| 58 | status 5 | unsupported property |
| 69 | 0 | unknown |

- Ping (op 0x12, empty): status 0.

## Stage C: read-only backup (works in mode 1, no host mode needed)

- File 15 (profile list): `13 00 01 00 60 6d` → one profile, id 28000.
- File 28000 (profile): name "GALLEON 100 SD Default Profile"; properties → 28001, lighting list → 28002, next pointer 28003 (assignments, not yet followed).
- File 28002 (lighting list): `69 6c 02 00 | 08 00 65 6d | 08 00 64 6d` → two layers, 28005 and 28004, flag 0x08 (start with profile).
- File 28004 = **Static**:
  - Effect `7e 20`.
  - Color bytes `ff 05 ff 12` = A,B,G,R → **RGB(18, 255, 5) green**.
  - 103 keys.
- File 28005 = **Type Lighting Key**:
  - Effect `f9 b1`, speed 5.
  - Colors **RGB(255, 41, 79)** and **RGB(255, 38, 0)**.
  - 126 keys.
- Key ids are USB HID usages (0x04 = A … 0x52), plus extended ids (0x64–0x91).
- Files 38 (serial, 35 B) and 31000 (device id, 16 B) also saved.
- Opening a nonexistent file returns **status 6**. Harmless: the probe's parser followed wrong pointers inside non-profile files. Fix before the next backup.

## What this changes for G2

1. Reports are 128 bytes: first write chunk carries 128 − 9 = 119 data bytes, continuations 123.
2. Firmware 2.3.88 matches GalleonCore's failed LampArray tests, **not** galleon-deck's 3.70.1 success. If 0x22 / 0x01 color writes do nothing on 2.3.88, a firmware difference is the first suspect, not our framing.
3. File reads don't need host mode, so a full backup (assignments 28003 and its action files too) can be completed safely before G2.
4. Typing check after the run: pending his confirmation.
