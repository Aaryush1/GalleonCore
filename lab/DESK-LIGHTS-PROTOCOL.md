# Desk lights: plugin ↔ helper protocol (v1)

The Stream Deck plugin (`streamdeck-info`, the brain) tells the helper (`lab/tools/DeskLights`, the hands) what each zone should look like. The helper owns the devices and every animation.

## Transport

- Named pipe `\\.\pipe\desk-lights`. The helper is the server and the plugin is the client. Local only; no port and no secret.
- UTF-8 JSON, one object per line (`\n`). Lines over 64 KiB are dropped and the connection is closed.
- One client at a time. A new client replaces the old connection.

## Plugin → helper

```json
{"type":"hello","client":"streamdeck-info","protocol":1}
{"type":"heartbeat"}
{"type":"scene","seq":12,"base":"#172554",
 "fkeys":[{"color":"#2563eb","effect":"steady"},null,null,null,null,null,null,null,null,null,null,null],
 "leftDial":{"color":"#f59e0b","effect":"steady"},
 "typing":{"glow":"#ffffff","fadeMs":350}}
```

| Field | Rule |
|---|---|
| `hello` | First line after connecting. A `protocol` other than 1 gets a status with an error, and the scene is ignored. |
| `heartbeat` | At least every 5 s. Any line counts as a heartbeat. |
| `scene.seq` | Increases by one with each new scene. The helper echoes the last one it applied. |
| `scene.base` | `#rrggbb`. Every key LED not set by another zone. |
| `scene.fkeys` | Exactly 12 entries, F1..F12 (LEDs 0x3A..0x45). `null` = base. `effect` is `steady` or `pulse` (about 1 s cycle, between 35% and 100%). |
| `scene.leftDial` | `effect` is `steady`, `pulse` or `off` (dial dark). `color` is required unless the effect is `off`. |
| `scene.typing` | `glow` `#rrggbb`. `fadeMs` 0..3000; 0 turns the glow off. |

**Validation is all or nothing.** A scene with a missing field, a bad color, a wrong `fkeys` length, an unknown effect or an unknown top-level field is rejected whole. The helper keeps showing the previous scene and reports the error in a status line. It never shows half a scene.

## Helper → plugin

```json
{"type":"status","keys":"live","dials":"ours","seq":12,"error":null}
```

- `keys`: `live`, `yielded` (Web Hub or another host has the keyboard), `unplugged`, `frozen` (the freeze policy tripped; needs a replug) or `absent` (no keyboard found).
- `dials`: `ours`, `not-ours` (Windows gave Dynamic Lighting control to another app, or our package isn't the background controller) or `absent`.
- `seq`: the last scene applied, or `null`.
- `error`: the last rejected line's reason, or `null`.

The helper sends a status on connect, whenever a field changes, and every 30 s.

## When the plugin goes quiet

No line for **30 s** means the plugin is stale. The helper switches to the **neutral scene**: base `#115e59`, all F-keys base, left dial off, and the typing glow kept as it was. A fresh scene ends the fallback. The neutral scene is also what the helper shows before any scene has arrived.

## Ownership

- Keys, Copilot key and top strip: GalleonLink (`Keyboard`), with all its safety rules (release on exit and suspend, yield to Web Hub, reconnect, 20 fps cap). The helper starts `Guard` with its own PID.
- Dials (and later the mouse): Windows Dynamic Lighting, `LampArray` with PID 11032. Left dial = lamp 0, right dial = lamp 1. The helper never touches the keyboard's own LampArray (PID 11020); those keys ignore it anyway.

## Version 2 (2026-10-25, overnight build; not live yet)

Additive. A v2 helper accepts `hello` with `protocol` 1 or 2. A v1 scene stays valid. A v2 plugin sends `protocol: 2` and only sends v2 fields after a `status` with `"protocol":2` (a v1 helper never sends that field), so a new plugin never gets its scenes rejected by an old helper.

**Optional scene fields (v2).** When absent, each takes the default shown.

| Field | Default | Rule |
|---|---|---|
| `rightDial` | `{"effect":"off"}` | A zone like `leftDial`. Needs you: amber breathe, amber steady, red, or one green blink. |
| `copilot` | `null` (base) | A zone on LED 0x81, the Copilot key. Mirrors `rightDial` (#23). `effect` is `steady`, `pulse` or `off`. |
| `breathe` | n/a | A new `effect` value, allowed for any zone: a slow 4 s cycle between 20% and 100% ("waiting on you"; `pulse` stays the 1 s "urgent" cycle). |
| `blink` | n/a | A new `effect` value: show the color for 600 ms after the scene with this `seq` first arrives, then behave as `off` (dials) or base (keys). Used for "a session finished". |
| `strip` | `null` | Either `null` (base) or exactly 16 entries for LEDs 0x82..0x91, each `#rrggbb` or `null`. Raw LEDs; the plugin owns the mapping once the strip is mapped. |
| `keysLevel` | `1.0` | 0.1..1.0. Multiplies the base color only (not F-keys or other zones). 0.4 while the mic is live. |
| `mouse` | `null` | `null` = don't drive the mouse (leave it to G HUB). Or a zone (`steady` / `pulse` / `breathe` / `blink` / `off`) on all 8 mouse lamps (Dynamic Lighting, mouse PID 16537). Rules: the battery warning (amber below 20%, red pulse below 10%), one sweep at under 2 min, logo red on mic live, the away replay. |

**Status (v2)** adds `"protocol":2` and `"mouse":"ours"|"not-ours"|"absent"|"idle"`, where `idle` = not driving it.

**Helper → plugin events (v2):** `{"type":"key","usage":58}` is **not** sent. Key codes never leave the helper.
