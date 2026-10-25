# CORSAIR Web Hub v1.2.42 protocol notes (Galleon 100 SD, VID 0x1B1C PID 0x2B0C)

Source: public front-end bundle `assets/B1aWXCAu.js` (minified, 2.4 MB) plus `icueWebSocketWorker-CW8boWBS.js`.
Method: grep/regex only, nothing executed. Minified names (`Di`, `Xw`, `w`, `Lt`) are bundle-local and will change between builds.
Everything below is read from static code. Nothing is verified against a real device unless it says so.

## 0. Headline findings

- Web Hub has **no live or streaming lighting path**. Every lighting change is a persistent file write (layer file, lighting list file, profile cookie), then a mode switch back to SELF_OPERATED. Searches for preview, temporary, realtime, colorBuffer and openColorEndpoint found nothing.
- The only "instant" lighting-related writes are scalar properties: brightness (property 2), indication-colour enables (99/155/156/278) and game-mode lighting (227/228/234).
- Web Hub never lists files. It follows the tree: profile list (file 15), then each profile file, then the file ids inside it. A read-only backup can walk the same tree.
- Frozen or Invalid_State handling is thin: status 3 is treated like "session expired", so it starts a new session and retries once. See section 8.
- The HID filter is usagePage 0xFF42 (65346), usage 1, report id 0, 64 bytes (`AH=1,IH=65346,_H=64`).
- Web Hub also talks to iCUE over `ws://127.0.0.1:50123/` and asks it to `release_device` before using the HID device. See Unknowns.

## 1. Framing (Bragi 1.5, 4-byte header)

Header builder `M2e.getHeaderCommand(e=true, cmd, session, sub)`:

    a[0]=sub (device/sub-device address, 0 for a direct device)  a[1]=1  a[2]=session  a[3]=cmd

The padded report is 64 bytes. Session byte injection happens in `hidTransport`:

    if(n===V1_5 && r[3]!==Xw){ l=new Uint8Array(r); l[2]=this.getSessionId(e)??0 }   // Xw=27

So every command except opcode 27 gets the stored session id in byte 2. It is 0 before the first session.

Reply layout:

- byte0 is the sub address and byte1 is a flag (15 means session expired).
- byte3 echoes the cmd and is used for matching.
- byte4 is the status and data starts at byte5. `Si(4)={defaultLength:5, commandStatus:4, commandValue:5}`.
- Unsolicited notifications use the same endpoint: id = byte3, data from byte4.

Timing and constants:

- `commandDelayMs=12` is slept before every send.
- `timeoutMs=3000` and `maxRetries=0` by default. Only product 11012 and the 11071-11077 ids get different values.
- `maxRestarts=1`.
- A global mutex serialises all commands.
- The default report length is 64 (`_H`) unless the HID descriptor says otherwise.

Status codes (`xK`): 0 Success, 1 Invalid_Argument_Value, 2 Insufficient_Buffer_Size, 3 Invalid_State, 4 Command_Not_Supported, 5 Property_Not_Supported, 6 Invalid_Device_Address, 7 Hardware_Error, 9 Invalid_Operation, 10 Invalid_Firmware_File, 11 Unknown.

DeviceIndex enum for the old 2-byte header (not used by the Galleon, which is 1.5): Device_Itself=8, Device_Itself_Suppress_Answer=0, First_Paired_Device=1.
`Device_Itself_Suppress_Answer` is only reachable through `getHeaderCommand(false, ...)` on the 2-byte header class. A grep found **no call site that passes false**, so Web Hub never sends a no-reply command.

Protocol detection: `Di.getBragiVersion()` is a get of property 261 (`t2e`) with header (sub 0, session 0). Reply data[0]+data[1]*0.1 === 1.5 means VER1_5.
The Galleon entry has `supportMultiSessionControl:true`, so 1.5 is used.

## 2. Opcodes and the `Di` methods that build them

Constants are `U2e=1, iL=2, j2e=3, K2e=5, H2e=6, G2e=7, z2e=8, W2e=9, $2e=10, Y2e=11, q2e=12, Z2e=13, X2e=15, aL=18, Xw=27`.
Opcodes 4, 14 and 16-17 have no builder, so they are unknown. "H" below means the 4-byte header.

| Op | Di method | Payload after H | Reply data (from byte 5) | Class |
|---|---|---|---|---|
| 1 | `setCommand(prop,val4,session,sub)` | propId LE16, value 4 bytes (zero padded or truncated to 4) | none | **WRITE** |
| 2 | `getCommand(prop,session,sub)` | propId LE16 | first 4 bytes = value | read |
| 3 | `getMultiplePropertyCommand([ids])` | count u8, then ids LE16 each | unknown layout | read |
| 5 | `unbindBufferCommand(session,[bufs],sub)` | count u8, then buffer indices | none | state (releases buffer) |
| 6 | `writeBufferBeginCommand` | buf u8, totalLen LE32, first chunk of data | none | **WRITE** |
| 7 | `writeBuffer` (continue) | buf u8, data chunk | none | **WRITE** |
| 8 | `readBufferCommand` | buf u8 | next chunk of file bytes (up to 59) | read |
| 9 | `describeBufferCommand` | buf u8 | `data.slice(2,6)` = size LE32 | read |
| 10 | `calibrationControlCommand` | action u8 (1 Start, 2 Abort, 3 Reset, 4 Apply), type u8 (0..8, 8=MagneticSwitch) | - | **never** |
| 11 | `createFileCommand` | fileId LE16 | - | **WRITE** |
| 12 | `deleteFileCommand` | fileId LE16 | - | **DESTRUCTIVE** |
| 13 | `openFileCommand(session,fileId,buf,sub)` | **buf u8, fileId LE16** (buffer first) | - | read (binds a buffer) |
| 15 | `resetToFactorySettingCommand` | mode u8 (0 Everything, 1 Property, 2 File) | - | **never** |
| 18 | `pingCommand` | none | status only | read |
| 18 | `pairingCommand` (same opcode) | pairType u8, RF=0, NO_SECURE=0 | - | **never** (with a payload) |
| 27 | `start/stopHostSoftwareSessionControlCommand` | start: `[1, rnd4, 0]`; stop: `[2, 0,0,0,0, sessionId]` | start: session id at reply byte 9 | session |

Evidence for the opcode 13 layout and the session commands:

    openFileCommand(e,n,r,i){const a=...getHeaderCommand(!0,Z2e,e,i),o=new Uint8Array([r,...k.fromNumberToBytesLE(n,2)])
    startHostSoftwareSessionControlCommand(e,n){... getHeaderCommand(!0,Xw,0,n) ... new Uint8Array([1,...i,0])
    stopHostSoftwareSessionControlCommand(e,n){... getHeaderCommand(!0,Xw,e,n) ... [2,...new Uint8Array(4),...fromNumberToBytesLE(e,1)]

`stopHostSoftwareSession...` has **no call site** in the bundle. Web Hub never stops a session; it just lets it expire.

Chunking (`writeBufferBegin` wrapper):

- First chunk = reportCount (64) - (5 + headerSize 4) = **55** data bytes.
- Continuation chunks (op 7) carry 59 data bytes (`h=h+4`).
- The `describeBuffer` plus `readBuffer` loop reads until the described size is reached, concatenating `data` each time.

Higher-level file operations in the device service (`w`, class `xg`):

- `readFile(fileIdLE2, buf=0)` does `openFile`, then `describeBuffer`, then a `readBuffer` loop, then `unbindBuffers([buf])` in `finally`. It first calls `setOperatingMode(2)` and restores 1 afterwards **only if** the device entry has `configInHostControl:true`. The Galleon entry has **false**, so `readFile` itself never changes mode.
- `writeFile`: `openFile`; if that fails then `unbind`, `createFile` and `openFile` again; then `writeBufferBegin`; then `unbind` in `finally`.
- `openFile` retry: if the status is Invalid_State (3) it runs `unbindBuffers([n])` and retries the open once.
- Buffer ids are clamped: `a = a>3 ? 1 : a`. Callers use `BufferIndex.Common_File_Index = 0` and sometimes `buf++` to use buffers 1-3.
- `clearFile` writes a single byte `[eH=0]`.

## 3. Properties (getCommand/setCommand ids)

Names are inferred from the call sites. The constants live in an anonymous `const` block (`eH=0,b3=1,gR=2,Uhe=3,...`) with no names. **Do not confuse these with the `bi` enum** (`Brightness=7, SleepMode=8, WinLockIndicationColor=93 ...`), which is the Web Hub device-settings UI field id, not the property id.
Values are 4 bytes LE unless noted. Colour properties are stored **B,G,R,0** (code does `.slice(0,3).reverse()` to get RGB).

| Prop | Meaning (evidence) | Direct get | Direct set |
|---|---|---|---|
| 1 | USB polling rate (`getProperty(b3,..)`) | yes | yes |
| **2** | **RGB brightness**: `getBrightness = u16/10` giving 0-100; `setBrightness: Math.round(pct/10)*100` so 0..1000 in steps of 100 | yes | yes (live) |
| **3** | **Operating mode**: 1 SELF_OPERATED, 2 HOST_CONTROLLED, 3 BOOTLOADER, 4 SLEEP, 5 MCU_BOOTLOADER, 6 LIMITED_HOST_CONTROLLER, 7 ESPORTS | yes | **yes, see section 9** |
| 4 | "debug mode" (profile property map) | file | - |
| 11 | power-saving mode on/off | yes | yes |
| 13, 14, 55 | auto-sleep enable / timeout (14 and 55 pick the timeout variant) | yes | yes |
| 15, 17, 18 | battery level and dongle-related (wireless only) | yes | - |
| 19 | firmware version | yes | - |
| 54 | sub-device-change notification property | notif | - |
| 58 | connection mode (0 USB, 1 RF, 2 BLE1, 3 BLE2, 6 RF_BLE, 7 USB_BLE, 8 BLE3) | yes | - |
| 62 | free storage in bytes (compared with a 5 MB image package) | yes | - |
| 65 | physical layout (1 ANSI, 2 ISO, 3 BR, 4 JP, 5 KR) | yes | - |
| 69 | Winlock on/off | yes | yes |
| 73 | **profile colour** (RGB, in the profile property file, id `Kw`) | file | file |
| 93 | WinLock **indication colour** (property file) | file | file |
| 99 | WinLock indication-colour enable: 2 = on, 0 = off | yes | yes |
| 108 | active dial mode index (property file) | file | - |
| **110** | active hardware profile index | yes | yes (`setActiveHardwareProfileIndex`) |
| 120 | console host operation | yes | - |
| 150 | max polling rate | yes | - |
| 155 / 156 | CapsLock / ScrollLock indication-colour enable (2 / 0) | yes | yes |
| 158 / 159 | CapsLock / ScrollLock indication colour (property file) | file | file |
| 207 | **Fn-action indication colour** (BGR0) | yes | yes (`setProperty(h7,0,false,{dataset})`) |
| 215 | motion-based power saving | yes | yes |
| 216 | device case colour | yes | - |
| 223 | polling rate (non-USB variant) | yes | - |
| 225 | game mode enable | yes | yes |
| 227 / 228 | game-mode lighting override enable / **colour** (BGR0) | yes | yes |
| 230 | screen module present | yes | - |
| 234 | game-mode **brightness** (0-1000) | yes | yes |
| 238 / 239 | game-mode switch animation / extra game-mode flag | yes | yes |
| 240 | screen startup animation active | yes | - |
| 242 / 243 | LCD screen width / height | yes | - |
| 250-260 | game-mode actuation/flash-tap sub-properties; 251-255 are flash-tap in the profile property file | yes | yes |
| 261 | Bragi protocol version (major, minor in data[0], data[1]) | yes | - |
| 263 | active screen-mode index (property file) | file | file |
| **265** | **active theme index** (property file). `setActiveThemeIndexAsync` writes it through `updatePropertyProfile` into the profile property file, not through op 1 | file | file |
| 278 / 279 | rapid-trigger indication-colour enable / **colour** | yes | yes |
| 291 | dynamic dial screen-position index | yes | - |
| 298 | 5-bit bitmask of something (read after HOST mode) | yes | - |

Mouse-only ids (not relevant to the Galleon): 6, 7, 8, 10, 56, 24-31, 47-52, 79-81, 164-165, 275, 276.

Not found by name: "LED count" and "layout" as live properties. The LED count is **static config** (`ledCount:146`, see section 6). "configInHostControl" is a per-device boolean in the front-end device table, not a protocol field.

Brightness evidence:

    async getBrightness(e){const n=await w.getProperty(gR,e),r=new Uint8Array(n.slice(0,2));return k.fromBytesToNum(r)/10}
    async setBrightnessAsync(e,n){if(n<0||n>100)return jt.FAILED;const r=Math.round(n/10)*100;if(await w.setProperty(gR,r)){...

Note that `setBrightness` does **not** switch to HOST mode first. Persistence across a power cycle is unknown.

Property map (per-profile "properties" file) format: `[2-byte header][u16 count]` then `count x { propId LE16, value 4 bytes }`.
The Galleon default profile carries ids 74, 93, 94, 207 and 265 (`properties:[{id:74,value:1},{id:93,value:"#ff0000"},{id:94,value:"#ffffff"},{id:207,value:"#ffffff"},{id:265,value:1}]`). Properties 74 and 94 are unnamed.

## 4. Files, buffers, and how Web Hub reads them back

Buffer index: only `BufferIndex.Common_File_Index = 0` is defined. The real range used is 0-3.

Well-known file ids (the file-id namespace is separate from property ids):

| File id | Content (evidence) |
|---|---|
| **15** (`Cd`) | **Profile list**: `[header LE16 = fx = 19][count LE16][fileId LE16 x count]` (`convertBytesToProfilesListFormat`) |
| 19 (`rL`) | lighting list pointer for non-profile devices (C2); not used by the Galleon |
| 36 | connected sub-devices (dongles) |
| 38 | serial number (`l2e`; text from offset 3) |
| 53 | pairing id |
| 61 | screen resource map |
| 62 | screen widget copy target |
| 72 | actuation; 75 = magnetic switch settings (calibration) |
| 28000-32767 | user allocation range (`findUnusedNumber`, start 28000, skips 28049). Profile ids are valid in 25000-32766 (`isInvalidProfileId`); default profile id `Cg = 28000` |
| 28006, 28007 | default screen resources |
| 30996-30999 | `customDialListing`, `keyStrokeDialListing`, `disableKeyListing`, `keyStrokeListing` (Galleon `fileIds`) |
| **31000** | `deviceId` file |

**Profile file** (file id = profile id), `convertBytesToProfileFileFormat`, byte offsets:

    0-1 header | 2-5 cookie LE32 | 6-7 assignmentsFileIndex | 8-9 propertiesFileIndex | 10-11 archiveFileIndex
    12-13 lightingsFileIndex | 14-15 keyMappingsFileIndex | 16-17 coolerssettings | 18-19 dialModessettings
    20-21 gamepadSensitivityCurves | 22-23 firmwareSpecificData | 24-25 primaryActuationDistance
    26-27 secondaryActuationDistance | 28-29 lightingChannelsPlaybackStrobingSettings | 30-31 nameLength LE16
    32..32+nameLength  name (UTF-8, NUL terminated, so nameLength+1 bytes) | then 4 x LE16: primaryActuationResetDistances,
    secondaryActuationResetDistances, rapidTriggerSettings, screenModesLayoutFile

Each index is a file id LE16 (0 means none). The cookie is `Math.floor(Date.now()/1000)`, regenerated by `updateCookie` after any write, so Web Hub can detect that the device changed (`isUpdateProfile` compares the device cookie with its IndexedDB copy).

**Lighting list file** (`lightingsFileIndex`), `fromByteArrayToLightingList`:

    [header LE16 = RL = 27753 = 0x6C69][count LE16] then count x 4 bytes: [flags u8 (bit3 = StartWithProfileActivation => 0x08)][0][layerFileId LE16]

Flags enum `Tw`: StartOnPress 0, StopOnPress 1, StopOnRelease 2, StartWithProfileActivation 3 (bit positions).
Max layers: `lightingLayerMax: 10` for the Galleon.

**Read-only backup walk** (this is what `exportProfileAsync` does; it also sets HOST first):

1. Read file 15 to get the profile ids. (The Galleon's `profileMax` is 5.)
2. For each profile id, read the profile file and parse the offsets above.
3. Read each non-zero file id: properties, lightings, assignments, keyMappings, archive, dialModes, actuation files, screenModesLayout.
4. From the lighting list file read every layer file id. From the assignments file read the action files (`parseAssignmentActionsV3`: after the 4-byte header, per group skip 1, read a count, then per entry skip 2, file id LE16, skip 4 more bytes).
5. From the dial-mode file: header 2, count LE16, per entry skip 5, count u8, per sub-entry skip 4 then file id LE16.
6. Optionally read the device-wide files 30996-31000.

Evidence of the export walking the lighting list:

    const O=await w.readFile(u.lightingsFileIndex,i);if(O.length>0){g.lighting=...;const M=new DataView(O.buffer);let F=2;const N=M.getUint16(F,!0);F+=2;
    for(let V=0;V<N;V++){F+=2;const Y=new Uint8Array(O.buffer.slice(F,F+2)),K=await w.readFile(Y,i); ...F+=2}

## 5. Lighting layer (effect) file format

`toByteArrayFromLightingEffect` writes, and `fromByteArrayToLightingEffect` reads:

    [effectType 2 bytes][flags u8][speed u8][direction u8][0x00][colorCount u8][colorCount x 4 bytes][keyCount u8][keyCount x keyCode u8]

Effect type bytes (`Dw`, 2-byte magic per effect):

| Effect | bytes |
|---|---|
| **static** (id 1, 1 colour, "Specified colors") | `[126, 32]` |
| solid | `[135, 0]` |
| horizon (id 0) | `[101, 0]` |
| watercolor | `[34, 0]` |
| type | `[40, 0]` |
| spiralRainbow | `[135, 171]` |
| colorWave | `[255, 123]` |
| visor | `[192, 144]` |
| rain | `[126, 160]` |
| typeRipple | `[162, 9]` |
| typeGradient | `[249, 177]` |
| rainbowWave | `[76, 185]` |
| colorShift | `[250, 165]` |
| colorPulse | `[79, 173]` |
| gradient | `[244, 235]` |
| ripple | `[174, 10]` |
| wave | `[234, 31]` |
| rainbowLighting | `[2, 164]` |

- **Flags byte**: bit positions via `getSelectedFlags`. In the front-end effect table, "Specified colors" and "Random colors" are the options. Exact bit meaning is uncertain.
- **Speed**: levels 3 Slow, 4 Medium, 5 Fast, 0 N/A. **Direction**: 1 UpToDown, 2 DownToUp, 4 LeftToRight, 5 RightToLeft, 6 Clockwise, 7 CounterClockwise.
- **Colours**: 4-byte groups, written **reversed** (`g=[...u.reverse()]`, the whole flat array, except watercolor). The reader reverses each 4-byte group. For a single colour the on-wire order is therefore the reverse of the UI array, i.e. **A,B,G,R** if the UI array is [R,G,B,A]. For multi-colour effects the colour order flips in the writer but not in the reader; treat multi-colour as unresolved.
- **Keys**: `keyCount` then one byte per LED id.

Layers are persistent: `writeFile(layerId, bytes)`, then the lighting list file, then `updateCookie`, then `setOperatingMode(1)`.

## 6. LED / key id table (Galleon)

The LED id is the **USB HID keyboard usage code**, with modifiers remapped. Map `T7/jg`: **224-230 (LCtrl, LShift, LAlt, LGui, RCtrl, RShift, RAlt) become 105-111**. 122 = FN.

From the Galleon default-profile config (config id 6, named "GALLEON 100 SD Default Profile") in the bundle:

    "ledCount":146, "keys":{"count":125,"data":[4..101,105,106,107,108,109,110,111,115,116,117,118,119,122,124,129,130,131,132,133,134,135,139,140,141,142,143,144,145]}
    indicationLeds ANSI: "4-49, 51-82, 101, 105-108, 110-111, 122, 129"
    ISO adds 50 and 100; BRAZILIAN adds 115; JAPANESE adds 115-119

Reading the ANSI string as the physical key set, the Galleon has these LEDs:

- HID 4-49 and 51-82 (letters, digits, punctuation, Esc, F1-F12, PrtSc, nav, arrows).
- 101 (Application key).
- 105-108 and 110-111 (modifiers; no RCtrl 109 on ANSI).
- 122 (FN).
- 129 (the Copilot key, `keyMappings:[{index:129,name:"Copilot"}]`).

The default template list also includes 83-100 (keypad range) and 124, 130-135, 139-145. Their physical position on the Galleon is **not labelled** in this bundle.
The Galleon US layout in the key-assignment table lists F1 (58) as game mode, F2 profile, F3/F4 backlight, F5-F8 media, F9-F11 mute/volume, 16 (M) macro, 110 flash tap, 108 Winlock, 79/80 screen left/right, 122 FN.
`rotaryDialIds:[0..9]` for the Galleon refers to dial-mode types, not LEDs. No separate LED ids for the 3x4 LCD pad or the dials were found; the LCD keys have no lighting entries.
Per-key names for the codes match the standard HID table (e.g. `{code:4,name:"KeyA"}`, `{code:44,name:"Space"}`, `{code:40,name:"Enter"}`).

## 7. Live / temporary lighting

Result: **none**. Specifically:

- Searched for previewLight, temporary, realtime, real-time, software lighting, colorBuffer, openColorEndpoint, liveLight, LightingPreview and directMode; zero hits. The only hits for "endpoint" are the iCUE WebSocket messages `ping`, `release_device` and `restore_device`.
- The lighting page selects keys and previews colours client-side. The device is only touched by `addOrUpdateLightingEffectAsync` / `updateLightingEffectsAsync` (file writes).
- Closest "immediate" writes: property 2 (brightness), 227/228 (game-mode lighting override + colour), 99/155/156/278 (indication enables), 207 (Fn indication colour), profile colour 73 and theme 265 (both in the property file).
- Any real-time per-key colour stream would have to be reverse-engineered from iCUE or CUE SDK traffic. It is not in Web Hub.

## 8. Frozen / invalid-state handling in Web Hub

`validateResponse(headerSize, cmd, reply)` runs in this order:

1. `commandIdMismatch` if `reply[3] !== cmd[3]`. This throws "Command ID mismatch", with no retry.
2. `expired` if `reply[1] === 15`.
3. `invalidState` if `reply[4] === 3`.
4. Otherwise `ok`.

`sendCommand` loop for expired or invalidState:

    case"expired":case"invalidState":{ if(S>=d) return {ok:!1,status:"error",error:new Error("Expired too many times")}; S++;
      const T=r[0], P=await this.startSession(e,n,T,...) ; break }   // then the loop resends the same command

- `startSession` sends op 27 `[1, 4 random bytes, 0]` with session 0 and sub = the original `r[0]`, then stores `reply[9]`. The loop then resends the original command with the new session id.
- Only **one** restart per command (`U3=1`).
- No `stop session` is ever sent.
- Transport errors (timeout 3 s) are returned as `{ok:false,status:"error"}` and surface as thrown errors in the service. There is no device reset or reopen logic for a hung device.
- Per-operation cleanup: `readFile`/`writeFile` always `unbindBuffers([buf])` in `finally`. `openFile` on Invalid_State unbinds the buffer and retries the open once.
- The service layer ends every operation with `setOperatingMode(SELF_OPERATED)` in `finally`. That is the practical "recover" step.
- `triggerDevicesFrequency` pings all connected devices every 30 s (`fye=3e4`), only when the queue is idle. Ping is sent **twice** (the first reply is discarded).
- Web Hub also asks iCUE to release the device before the first command (`sendReleaseDevice()` runs inside every `sendCommand`).

## 9. Probe guidance

### Read-only commands safe for a probe

- Op 27 sub 1 (start host software session): a state change on the device, but Web Hub does it automatically on the first command. Use random 4 bytes. Keep one session and do not stop it.
- Op 2 get of: 261 (Bragi version), 3 (operating mode), 110 (active profile), 2 (brightness), 19 (firmware), 58, 65, 62, 69, 225, 227, 228, 234, 99, 155, 156, 278.
- Op 18 ping with an empty payload (header only).
- File reads through the sequence 13 (open, buf 0), 9 (describe), 8 (read, repeated), 5 (unbind buf), for ids 38 (serial), 31000 (deviceId), 15 (profile list), each profile file, then each id in it.
- Always finish with op 5 unbind of the buffer, even on error.

### Commands never to send

- Op 1 with prop 3 (operating mode) values 3 (BOOTLOADER), 4, 5 (MCU_BOOTLOADER), 6, 7. Value 2 (HOST) is what Web Hub uses, but treat it as risky until proven (see Unknowns).
- Op 12 (delete file), op 15 (factory reset), op 10 (calibration), op 18 with a payload (pairing), op 11 (create file) and ops 6/7 (writes) until the read-only backup is complete and verified.
- Op 27 sub 2 (stop session): never used by Web Hub and untested.
- Anything that rewrites file 15 (`rewriteProfileListFile` rebuilds it from Web Hub's IndexedDB copy, so a stale cache could drop profiles).
- Opcodes with no builder (4, 14, 16, 17) and anything with an unknown payload layout (op 3 reply).

## 10. Unknowns

- Whether file reads on the Galleon work in SELF_OPERATED mode. Web Hub's profile services always set HOST (2) first, even though `configInHostControl` is false. The serial-number read (file 38) is the only read it makes without a mode switch, and that is at init. Try reads in mode 1 first, then 2.
- What HOST mode does to the lighting (freeze, blank, hold last frame) and whether leaving the device in mode 2 or with a bound buffer causes the freeze seen earlier. Not observable from static code.
- Notification vs reply disambiguation. Both use byte 3 as an id. Web Hub only checks `reply[3] === cmd[3]` and throws on a mismatch, so a key-press notification (id 2) landing during a get (op 2) could be mistaken for a reply. A probe should check more bytes (reply byte1 and the status/length) and discard stray reports.
- Reply layout for op 3 (multi-get) and op 27 (only byte 9 = session id is known).
- Whether the session id must be reused across opens/closes of the HID handle, and how long a session lasts (what triggers the "byte1 == 15" expiry is unknown).
- The reply to op 9: `data.slice(2,6)` = size implies two leading bytes whose meaning is unknown.
- Layer file flags-byte bit meaning, alpha semantics of the colour bytes, multi-colour order, and which key codes beyond 4-82 and 105-111 exist physically on the Galleon (83-101, 124, 129-145).
- Whether property 2 (brightness) and 227/228/234 persist across a power cycle.
- Properties 74 and 94 (present in the Galleon default profile) and 4 ("debug mode").
- iCUE: Web Hub calls `release_device`/`restore_device` over `ws://127.0.0.1:50123/` with `{pid,vid}`. If iCUE is running it may hold the interface or fight the probe. The exact exclusivity behaviour on Windows is unverified; the protocol for that socket was not read beyond those two messages.
- Whether `hidTransport` reads can pick up reports from other interfaces; Web Hub filters to usagePage 0xFF42 usage 1 only.
