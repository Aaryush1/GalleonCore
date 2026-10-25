# OpenRGB Corsair Peripheral V2 controller: findings for the Galleon 100 SD

Status: G0 research, read-only. Written in my own words. Caveat: files were read through WebFetch, which summarizes
the page with a small model, so hex values and byte indices below are second-hand. Re-verify against the raw files
(URLs at the bottom) before hard-coding anything. Line numbers are not available from that tool; I cite functions.

Key framing: the "V2" controller is OpenRGB's name for Corsair's newer wired/wireless peripheral protocol, which is
Bragi (the same family as the Galleon). BUT OpenRGB speaks it with a **2-byte header** (v1.0 style:
`[report 0][deviceId][cmd]...`). No 4-byte header, no session handshake (0x1B), no Galleon PID, anywhere in V2.

## 1. Sequence used to take software/direct control

All writes are 65-byte hid_write buffers (byte 0 = report ID 0, 64 bytes payload). Interface 1, usage page 0xFF42
for every V2 device (same page as the Galleon vendor interface). Read timeout 50 ms (3 ms "short" for LED streaming).

Byte layout of a command (buffer index, 0 = report ID):
`[1]=deviceId (0x08 wired, 0x09 wireless receiver)`, `[2]=opcode`, `[3..]=operands`.

Opcodes (enum `corsair_v2_cmd`): SET 0x01, GET 0x02, CLOSE/STOP_TX 0x05, WRITE_FIRST 0x06, WRITE_NEXT 0x07,
OPEN/START_TX 0x0D. (Matches the Bragi property-set / property-get / close-handle / write / continue-write / open-handle set.)

Steps, in order:
1. **Identify**: GET property 0x12 (PID, retried up to 8x with 120 ms delay because the device is often not ready right
   after enumeration; 0x00 and 0xFFFFFFFF count as failure). Reply: `read[2]`=status (nonzero = error), value =
   little-endian u32 at `read[3..6]`. For wireless-receiver PIDs the deviceId is switched 0x08 -> 0x09 and PID re-read.
   Also GET 0x11 (VID/capability) once, result discarded.
2. **Probe the lighting resource** (only if the table says the model uses the 0x22 resource and it is not a K100):
   OPEN with `[3]=handle 0`, `[4]=resource 0x22`; a nonzero status in `read[2]` makes it fall back to resource 0x01.
   Then CLOSE. (Replies are checked via `buffer[2]`.)
3. **Enter software mode**: SET property 3 (render mode) with value 0x02 at `[5]` (`[4]` left 0). Value 0x01 = hardware
   mode. The mode constant sits in `[5]` as a single byte. Both the hardware- and software-class constructors do this,
   then also send a GET with `[3]=0x5F` ("LightingControl"); purpose unknown (possibly a refresh/trigger query).
4. **Per frame** (`SetLEDs`):
   a. Drain any stale input reports (read with 3 ms timeout until empty).
   b. OPEN handle 0 on the lighting resource (`[2]=0x0D, [3]=0, [4]=resource`).
   c. First write packet: `[2]=0x06`, `[3]=0`, `[4..5]` = total payload length (u16 LE), payload starts at `[8]`
      (so up to 57 bytes in packet 1; bytes `[6..7]` stay zero).
   d. Continuation packets: `[2]=0x07`, `[3]=0`, payload starts at `[4]` (up to 61 bytes each), final chunk
      zero-padded. Only the first packet's reply is read; continuations are fire-and-forget with a 2 ms sleep.
   e. CLOSE: `[2]=0x05`, `[3]=0x01`, `[4]=0` (handle). Reply not checked.
5. **Release**: the destructor writes SET property 3 = 0x01 (hardware mode), then closes the HID device.
   A code comment says this matters because some models (K70 Core) stop reporting onboard events like the volume
   knob while in software mode until the device re-enumerates.

## 1b. Color buffer layouts (two variants, chosen per model)

| Resource | Used by | Payload |
|---|---|---|
| 0x01 ("colour blocks", SW class) | K55/K57/K60 PRO, mice, mousemats | planar: N reds, then N greens, then N blues; length = 3N; **no header** |
| 0x22 ("triplets", HW class) | K70 Core/Pro/TKL, K95 PLAT XT, K100, Katar etc. | 2-byte LE header = 0x0012 ("direct mode" id), then RGB triplets, 3 bytes per LED, length = 3N+2 (CORSAIR_V2HW_DATA_OFFSET) |

Dispatch is by the `light_ctrl` field (resource 1 vs 0x22), not by class name. The 0x22 payload header 0x0012 is the
same value as CORSAIR_V2_MODE_DIRECT, i.e. the mode/effect id the packet requests ("direct"). Other effect ids
exist in the enum (static 0x207E, spectrum 0x7BFF, ...) but the HW-effect path is commented out in this version;
a dead block shows an older layout of brightness then B,G,R at 4 bytes/color starting at offset 15.

Device-wide max write buffer in OpenRGB is 1024 bytes, but the HID write is always 65 bytes, so a ~125-key board needs
(3*125+2 = 377 bytes): 1 first packet (57) + ceil(320/61) = 6 continuations, about 7 reports/frame.

## 2. Volatility, keep-alive, host stops

- Nothing in the code writes to flash or issues a save/commit. Every frame is just open-handle, write, close on the
  lighting resource, with the device in render mode 2. I found no comment claiming it is volatile, so volatility is
  inferred, not stated. Given K70 Core users run this constantly without a persisted-lighting complaint, it is almost
  certainly RAM-only.
- Keep-alive: none in the V2 controller. Constants `UPDATE_PERIOD` 30000 and `SLEEP_PERIOD` 12500 exist in the header
  (likely used by the RGBController wrapper for a periodic re-send/idle re-entry, unverified).
  The iCUE Link controller also has no heartbeat.
- If the host just stops: untested/undocumented in source. Plausible that the device keeps the last frame until a
  firmware software-mode timeout, or reverts to hardware mode. Not answered by this source.
- Clean exit = set mode back to 0x01; the device then returns to its stored profile.

## 3. LED/key index mapping

- Not discovered from the device. The LED count is simply the number of colors OpenRGB passes (taken from a
  static per-PID table in `CorsairPeripheralV2Devices.cpp`). No "get LED count" property is used.
- Tables: two shared "value" grids (`corsair_full_size_values` 106 keys, `corsair_tkl_values` 89 keys, six rows)
  plus per-model keymap overlays that rename/swap/add entries. Each key has `{zone,row,col,index,name}`; `index`
  is the position in the color buffer (the wire LED index).
- Highest indices seen: K70 Core / Core TKL 122 (matrix 6x21 / 6x17), K70 PRO(V2) 141 (7x21), K70 TKL 128 (7x17),
  K100 192 (12x24 matrix with 69 overlay entries: media keys, G1-G6, 44 underglow 138-181, 8 wheel 182-189, logos).
- The buffer is dense by index (0..max); unused indices stay black. So the on-wire buffer length for K70 Core is
  roughly 123 LEDs even if fewer physical keys. Index order is Corsair's internal order, NOT row-major, so a per-model
  table (or empirical probing) is required. Layout (ANSI/ISO/ABNT/JIS) is read with GET 0x41 (1..4) and only
  selects an overlay for key names.

## 4. Header size / handshake

- Only 2-byte header `[deviceId, opcode]` after the report ID. deviceId 0x08 wired, 0x09 wireless, 0x80 for the
  K57 wired (special-cased, `skip_reads=true`, no replies read).
- No sessionId, no 0x1B handshake, no v1.5 detection. OpenRGB's 2-byte `[0x08][opcode]` can be read as `[subdevice][cmd]`
  of the Galleon's `[subdevice, 0x01, sessionId, cmd]` with the middle two bytes absent. In the Galleon framing the
  OPEN would plausibly be `[sub,0x01,sess,0x0D,handle,resLo,resHi?]` but resource-id width is unverified.
- The corsair-Link-style Bragi (iCUE LINK hub, same repo) uses `byte2=0x01` before the opcode, and a read header of 3 bytes
  / write header of 4 bytes, which is the closest in-repo analogue to v1.5 framing. Its constants (exact): OPEN_COLOR_ENDPOINT
  `0D 00`, OPEN `0D 01`, CLOSE `05 01 01`, WRITE_COLOR `06 00`, WRITE_COLOR_NEXT `07 00`, READ `08 01`,
  SOFTWARE_MODE `01 03 00 02`, HARDWARE_MODE `01 03 00 01`, SET_COLOR resource `22`, data type `12 00`. 513-byte write,
  512 read, 508 payload per request. Colors there are RGB triplets (not planar). Note SOFTWARE_MODE is the same SET
  property 3 = 2 as in V2. This independently confirms: property 3 mode, resource 0x22 color, triplets, handle open/write/close.

## 5. Dangerous commands

- None flagged anywhere in V2. No bootloader, reset, or firmware commands appear. Everything sent is: GET 0x11/0x12/0x41/0x5F,
  SET 3, OPEN/CLOSE handle, WRITE/WRITE-NEXT.
- Risky-by-nature items for the Galleon: SET 3 changes device mode (safe, reversible by writing 1; but if the process
  dies in mode 2, the keys' onboard behaviour and possibly the dial/LCD events may stay in SW mode until replug, per the K70
  Core comment). Always restore mode 1 in a finally/shutdown handler. Avoid writing to any handle/resource other than
  0x22 (resource ids for profiles/lighting files are persistent-storage backed, as Web Hub does).
- The unknown `GET 0x5F` is a GET, so likely harmless, but unexplained.

## 6. Nearest supported devices (PIDs)

2025-era wired Bragi keyboards with HW(0x22 triplet) path: K70 Core RGB 0x1BFD, **K70 Core RGB TKL 0x2B01** (closest in
PID range to the Galleon's 0x2B0C and same PID generation), K70 RGB PRO 0x1BC4, K70 PRO V2 0x1BB3, K70 RGB TKL 0x1B73 /
CS 0x1BB9, K100 optical 0x1B7C / 0x1BC5, K100 MX Red 0x1B7D, K95 PLAT XT 0x1B89, K60 PRO TKL 0x1BC7 / 0x1BED. Others in
the 0x2Bxx range: Ironclaw wireless SE 0x2B32, Slipstream Ironclaw SE 0x2B00. K100 (no resource probe, 12x24 matrix,
media/LCD-like extras) is the structural analogue for a keyboard with extra controls. **No Galleon / 0x2B0C anywhere.**
Sub-note: V2 detection is by VID 0x1B1C + interface 1 + usage page 0xFF42 (identical to the Galleon's vendor interface).

## Hypotheses for the Galleon (ranked)

1. **Try the OpenRGB K70 Core RGB TKL (0x2B01) flow, but with Galleon 4-byte framing.** Sequence: GET 0x12 (PID check) -> SET prop 3 = 2 (software mode)
   -> OPEN handle 0 on resource 0x22 -> WRITE `[0x12,0x00]+RGB triplets` -> CLOSE. Wrap each command in
   `[subdevice, 0x01, sessionId, ...]`. First experiment: does SET prop 3 = 2 return status 0 and change anything visible
   (e.g., onboard animation stops)?
2. **Use the iCUE-Link variant of the same flow**: WRITE_COLOR `06 00` / WRITE_COLOR_NEXT `07 00` with data type `12 00`
   (the 2-byte header 0x0012 in V2 triplets is this datatype). Likely the exact Bragi write header the Galleon expects.
3. **Resource 0x01 planar-RGB fallback** if 0x22 returns an error on OPEN (the probe logic V2 uses).
4. **Size the buffer with a sweep, not a table**: send N = 0..max triplets with one distinct color per index to discover which
   indices light which key; also try writing beyond K100's 192 to find dial/LCD-adjacent LEDs.
5. **Before sending anything, replay the Windows iCUE/Web Hub USB capture** (Wireshark/USBPcap) to learn the Galleon
   session-id handshake (0x1B) and confirm it matches the above; OpenRGB cannot tell us that.

## Unknowns

- Exact meaning/size of resource id and handle fields in the 4-byte-header protocol; whether OPEN takes resource as u8 or u16.
- The session handshake (0x1B) and how sessionId is allocated/validated; OpenRGB doesn't implement it.
- Whether `SET prop 3 = 2` is enough on Galleon, or whether Web Hub's lighting-file enable also gates it.
- What `GET 0x5F` does; whether it is needed to "arm" direct mode on newer firmware.
- What the device does when the host stops (timeout? last-frame hold?).
- Volatility is inferred, not proven; confirm by unplugging after writing, before trusting.
- Galleon LED count / index map (keys + dials + LCD slots), and whether the LCD slots/dial LEDs are on the same resource.
- Wire format whether `0x0012` header is a datatype, effect id, or both.
- All values above are via a summarizing fetch; verify raw bytes.

## Sources (all GitLab, branch master; paths are under Controllers/)

- https://gitlab.com/CalcProgrammer1/OpenRGB/-/raw/master/Controllers/CorsairPeripheralV2Controller/CorsairPeripheralV2Controller.cpp
  (constructor/PID probing, StartTransaction, StopTransaction, SetRenderMode, LightingControl, SetLEDs, destructor)
- .../CorsairPeripheralV2Controller/CorsairPeripheralV2Controller.h (opcode/mode/timing constants)
- .../CorsairPeripheralV2Controller/CorsairPeripheralV2HardwareController.cpp (triplets path, SetLedsDirect dispatch)
- .../CorsairPeripheralV2Controller/CorsairPeripheralV2SoftwareController.cpp (planar path)
- .../CorsairPeripheralV2Controller/CorsairPeripheralV2Devices.h / .cpp (PIDs, mode enum 0x01/0x02, LED tables)
- .../CorsairPeripheralV2Controller/CorsairPeripheralV2ControllerDetect.cpp (VID, interface 1, usage page 0xFF42, PID list)
- .../CorsairICueLinkController/CorsairICueLinkProtocol.h and CorsairICueLinkController.cpp (Bragi-like 4-byte write header,
  open/close endpoint, software/hardware mode, color endpoint chunking)
- Directory listing: https://gitlab.com/CalcProgrammer1/OpenRGB/-/tree/master/Controllers/CorsairPeripheralV2Controller
  (no K70 Core/K100/CorsairWireless-specific folders exist; those PIDs are in V2)
