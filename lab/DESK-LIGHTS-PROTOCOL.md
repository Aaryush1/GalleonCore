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

No line for **30 s** means the plugin is stale. The helper switches to the **neutral scene**: base `#0f766e`, all F-keys base, left dial off, and the typing glow kept as it was. A fresh scene ends the fallback. The neutral scene is also what the helper shows before any scene has arrived.

## Ownership

- Keys, Copilot key and top strip: GalleonLink (`Keyboard`), with all its safety rules (release on exit and suspend, yield to Web Hub, reconnect, 20 fps cap). The helper starts `Guard` with its own PID.
- Dials (and later the mouse): Windows Dynamic Lighting, `LampArray` with PID 11032. Left dial = lamp 0, right dial = lamp 1. The helper never touches the keyboard's own LampArray (PID 11020); those keys ignore it anyway.
